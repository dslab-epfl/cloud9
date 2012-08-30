//===-- AddressSpace.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AddressSpace.h"
#include "Memory.h"
#include "TimingSolver.h"

#include "klee/Expr.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/AddressPool.h"
#include "klee/CoreStats.h"

#include <sys/mman.h>

#include <glog/logging.h>

#include <algorithm>

using namespace klee;

///

void AddressSpace::bindObject(const MemoryObject *mo, ObjectState *os) {
  assert(os->copyOnWriteOwner==0 && "object already has owner");
  os->copyOnWriteOwner = cowKey;

  objects = objects.replace(std::make_pair(mo, os));
}

void AddressSpace::bindSharedObject(const MemoryObject *mo, ObjectState *os) {
  assert(os->isShared);
  assert(os->copyOnWriteOwner > 0);

  objects = objects.insert(std::make_pair(mo, os));
}

void AddressSpace::unbindObject(const MemoryObject *mo) {
  objects = objects.remove(mo);
}

const ObjectState *AddressSpace::findObject(const MemoryObject *mo) const {
  const MemoryMap::value_type *res = objects.lookup(mo);
  
  return res ? res->second : 0;
}

ObjectState *AddressSpace::getWriteable(const MemoryObject *mo,
                                        const ObjectState *os) {
  assert(!os->readOnly);

  if (cowKey != os->copyOnWriteOwner) {
    if (os->isShared) {
      ObjectState *n = new ObjectState(*os);
      n->copyOnWriteOwner = cowKey;

      for (cow_domain_t::iterator it = cowDomain->begin(); it != cowDomain->end();
          it++) {
        AddressSpace *as = *it;
        if (as->findObject(mo) != NULL) {
          as->objects = as->objects.replace(std::make_pair(mo, n));
        }
      }

      return n;
    } else {
      ObjectState *n = new ObjectState(*os);
      n->copyOnWriteOwner = cowKey;

      objects = objects.replace(std::make_pair(mo, n));
      return n;
    }
  } else {
    return const_cast<ObjectState*>(os);
  }
}

/// 

bool AddressSpace::resolveOne(const ref<ConstantExpr> &addr, 
                              ObjectPair &result) {
  uint64_t address = addr->getZExtValue();
  MemoryObject hack(address);

  if (const MemoryMap::value_type *res = objects.lookup_previous(&hack)) {
    const MemoryObject *mo = res->first;
    if ((mo->size==0 && address==mo->address) ||
        (address - mo->address < mo->size)) {
      result = *res;
      return true;
    }
  }

  if (VLOG_IS_ON(1)) {
    VLOG(1) << "Cannot locate address: " << address;
    VLOG(1) << "MEMORY MAP  " << std::string(50, '=');
    for (MemoryMap::iterator it = objects.begin(), ie = objects.end();
        it != ie; ++it) {
      std::string allocInfo;
      it->first->getAllocInfo(allocInfo);
      VLOG(1) << it->first->address << ": " << allocInfo;
    }
    VLOG(1) << std::string(60, '=');
  }

  return false;
}

bool AddressSpace::resolveOne(ExecutionState &state,
                              TimingSolver *solver,
                              ref<Expr> address,
                              ObjectPair &result,
                              bool &success) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    success = resolveOne(CE, result);
    return true;
  } else {
    TimerStatIncrementer timer(stats::resolveTime);

    // try cheap search, will succeed for any inbounds pointer

    ref<ConstantExpr> cex;
    if (!solver->getValue(data::SINGLE_ADDRESS_RESOLUTION, state, address, cex))
      return false;
    uint64_t example = cex->getZExtValue();
    MemoryObject hack(example);
    const MemoryMap::value_type *res = objects.lookup_previous(&hack);
    
    if (res) {
      const MemoryObject *mo = res->first;
      if (example - mo->address < mo->size) {
        result = *res;
        success = true;
        return true;
      }
    }

    // didn't work, now we have to search
       
    MemoryMap::iterator oi = objects.upper_bound(&hack);
    MemoryMap::iterator begin = objects.begin();
    MemoryMap::iterator end = objects.end();
      
    MemoryMap::iterator start = oi;
    while (oi!=begin) {
      --oi;
      const MemoryObject *mo = oi->first;
        
      bool mayBeTrue;
      if (!solver->mayBeTrue(data::SINGLE_ADDRESS_RESOLUTION, state,
                             mo->getBoundsCheckPointer(address), mayBeTrue))
        return false;
      if (mayBeTrue) {
        result = *oi;
        success = true;
        return true;
      } else {
        bool mustBeTrue;
        if (!solver->mustBeTrue(data::SINGLE_ADDRESS_RESOLUTION, state,
                                UgeExpr::create(address, mo->getBaseExpr()),
                                mustBeTrue))
          return false;
        if (mustBeTrue)
          break;
      }
    }

    // search forwards
    for (oi=start; oi!=end; ++oi) {
      const MemoryObject *mo = oi->first;

      bool mustBeTrue;
      if (!solver->mustBeTrue(data::SINGLE_ADDRESS_RESOLUTION, state,
                              UltExpr::create(address, mo->getBaseExpr()),
                              mustBeTrue))
        return false;
      if (mustBeTrue) {
        break;
      } else {
        bool mayBeTrue;

        if (!solver->mayBeTrue(data::SINGLE_ADDRESS_RESOLUTION, state,
                               mo->getBoundsCheckPointer(address),
                               mayBeTrue))
          return false;
        if (mayBeTrue) {
          result = *oi;
          success = true;
          return true;
        }
      }
    }

    success = false;
    return true;
  }
}

bool AddressSpace::resolve(ExecutionState &state,
                           TimingSolver *solver, 
                           ref<Expr> p, 
                           ResolutionList &rl, 
                           unsigned maxResolutions,
                           double timeout) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(p)) {
    ObjectPair res;
    if (resolveOne(CE, res))
      rl.push_back(res);
    return false;
  } else {
    TimerStatIncrementer timer(stats::resolveTime);
    uint64_t timeout_us = (uint64_t) (timeout*1000000.);

    // XXX in general this isn't exactly what we want... for
    // a multiple resolution case (or for example, a \in {b,c,0})
    // we want to find the first object, find a cex assuming
    // not the first, find a cex assuming not the second...
    // etc.
    
    // XXX how do we smartly amortize the cost of checking to
    // see if we need to keep searching up/down, in bad cases?
    // maybe we don't care?
    
    // XXX we really just need a smart place to start (although
    // if its a known solution then the code below is guaranteed
    // to hit the fast path with exactly 2 queries). we could also
    // just get this by inspection of the expr.
    
    ref<ConstantExpr> cex;
    if (!solver->getValue(data::MULTI_ADDRESS_RESOLUTION, state, p, cex))
      return true;
    uint64_t example = cex->getZExtValue();
    MemoryObject hack(example);
    
    MemoryMap::iterator oi = objects.upper_bound(&hack);
    MemoryMap::iterator begin = objects.begin();
    MemoryMap::iterator end = objects.end();
      
    MemoryMap::iterator start = oi;
      
    // XXX in the common case we can save one query if we ask
    // mustBeTrue before mayBeTrue for the first result. easy
    // to add I just want to have a nice symbolic test case first.
      
    // search backwards, start with one minus because this
    // is the object that p *should* be within, which means we
    // get write off the end with 4 queries (XXX can be better,
    // no?)
    while (oi!=begin) {
      --oi;
      const MemoryObject *mo = oi->first;
      if (timeout_us && timeout_us < timer.check())
        return true;

      // XXX I think there is some query wasteage here?
      ref<Expr> inBounds = mo->getBoundsCheckPointer(p);
      bool mayBeTrue;
      if (!solver->mayBeTrue(data::MULTI_ADDRESS_RESOLUTION, state, inBounds, mayBeTrue))
        return true;
      if (mayBeTrue) {
        rl.push_back(*oi);
        
        // fast path check
        unsigned size = rl.size();
        if (size==1) {
          bool mustBeTrue;
          if (!solver->mustBeTrue(data::MULTI_ADDRESS_RESOLUTION, state, inBounds, mustBeTrue))
            return true;
          if (mustBeTrue)
            return false;
        } else if (size==maxResolutions) {
          return true;
        }
      }
        
      bool mustBeTrue;
      if (!solver->mustBeTrue(data::MULTI_ADDRESS_RESOLUTION, state,
                              UgeExpr::create(p, mo->getBaseExpr()),
                              mustBeTrue))
        return true;
      if (mustBeTrue)
        break;
    }
    // search forwards
    for (oi=start; oi!=end; ++oi) {
      const MemoryObject *mo = oi->first;
      if (timeout_us && timeout_us < timer.check())
        return true;

      bool mustBeTrue;
      if (!solver->mustBeTrue(data::MULTI_ADDRESS_RESOLUTION, state,
                              UltExpr::create(p, mo->getBaseExpr()),
                              mustBeTrue))
        return true;
      if (mustBeTrue)
        break;
      
      // XXX I think there is some query wasteage here?
      ref<Expr> inBounds = mo->getBoundsCheckPointer(p);
      bool mayBeTrue;
      if (!solver->mayBeTrue(data::MULTI_ADDRESS_RESOLUTION, state, inBounds, mayBeTrue))
        return true;
      if (mayBeTrue) {
        rl.push_back(*oi);
        
        // fast path check
        unsigned size = rl.size();
        if (size==1) {
          bool mustBeTrue;
          if (!solver->mustBeTrue(data::MULTI_ADDRESS_RESOLUTION, state, inBounds, mustBeTrue))
            return true;
          if (mustBeTrue)
            return false;
        } else if (size==maxResolutions) {
          return true;
        }
      }
    }
  }

  return false;
}

// These two are pretty big hack so we can sort of pass memory back
// and forth to externals. They work by abusing the concrete cache
// store inside of the object states, which allows them to
// transparently avoid screwing up symbolics (if the byte is symbolic
// then its concrete cache byte isn't being used) but is just a hack.

void AddressSpace::copyOutConcretes(AddressPool *pool) {
  for (MemoryMap::iterator it = objects.begin(), ie = objects.end(); 
       it != ie; ++it) {
    const MemoryObject *mo = it->first;

    if (!mo->isUserSpecified) {
      ObjectState *os = it->second;
      uint8_t *address = (uint8_t*) (unsigned long) mo->address;

      if (!os->readOnly)
        memcpy(address, os->concreteStore, mo->size);
    }
  }
}

bool AddressSpace::copyInConcretes(AddressPool *pool) {
  for (MemoryMap::iterator it = objects.begin(), ie = objects.end(); 
       it != ie; ++it) {
    const MemoryObject *mo = it->first;

    if (!mo->isUserSpecified) {
      const ObjectState *os = it->second;
      uint8_t *address = (uint8_t*) (unsigned long) mo->address;

      if (memcmp(address, os->concreteStore, mo->size)!=0) {
        if (os->readOnly) {
          //Only cout structures are declared constant at the moment, they may be modified by system, try to continue
          LOG(WARNING) << "Read only object " << mo->name.c_str() << " written to by external. Leaving old state.";
          continue;
          //return false;
        } else {
          ObjectState *wos = getWriteable(mo, os);
          memcpy(wos->concreteStore, address, mo->size);
        }
      }
    }
  }

  return true;
}

void AddressSpace::DumpMemoryObject(std::ostream &os, const MemoryObject *mo,
                                    const ObjectState *ostate, bool fast) const {
  os << mo->address << "-" << mo->address + mo->size << " [" << mo->size << "] ";

	std::string alloc_info;
	mo->getAllocInfo(alloc_info, fast);
  os << "allocated at " << alloc_info;

  os << std::endl;
}

void AddressSpace::DumpContents(std::ostream &os) const {
  std::vector<const MemoryObject*> memory_vector;
  for (MemoryMap::iterator it = objects.begin(), ie = objects.end();
      it != ie; ++it) {
		const MemoryObject *mo = it->first;
    const ObjectState *ostate = it->second;
    memory_vector.push_back(mo);
  }
  std::sort(memory_vector.begin(), memory_vector.end(), MemoryObjectSizeLT());

	int largest_count = std::min(50, (int)memory_vector.size());

  os << "Largest " << largest_count 
		 << " memory blocks:" << std::endl;

  for (int i = 0; i < largest_count; i++) {
    DumpMemoryObject(os, memory_vector[i],
                     objects.find(memory_vector[i])->second, false);
  }

  os << std::endl;

	os << "All memory blocks:" << std::endl;

  for (MemoryMap::iterator it = objects.begin(), ie = objects.end();
      it != ie; ++it) {
    const MemoryObject *mo = it->first;
    const ObjectState *ostate = it->second;
    DumpMemoryObject(os, mo, ostate, true);
  }
}

/***/

bool MemoryObjectLT::operator()(const MemoryObject *a, const MemoryObject *b) const {
  return a->address < b->address;
}

bool MemoryObjectSizeLT::operator()(const MemoryObject *a, const MemoryObject *b) const {
  return a->size > b->size;
}

