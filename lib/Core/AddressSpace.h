//===-- AddressSpace.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ADDRESSSPACE_H
#define KLEE_ADDRESSSPACE_H

#include "ObjectHolder.h"

#include "klee/Expr.h"
#include "klee/Internal/ADT/ImmutableMap.h"

#include <ostream>

namespace klee {
  class ExecutionState;
  class MemoryObject;
  class ObjectState;
  class TimingSolver;
  class AddressPool;

  template<class T> class ref;

  typedef std::pair<const MemoryObject*, const ObjectState*> ObjectPair;
  typedef std::vector<ObjectPair> ResolutionList;  

  /// Function object ordering MemoryObject's by address.
  struct MemoryObjectLT {
    bool operator()(const MemoryObject *a, const MemoryObject *b) const;
  };
  
  struct MemoryObjectSizeLT {
    bool operator()(const MemoryObject *a, const MemoryObject *b) const;
  };

  typedef ImmutableMap<const MemoryObject*, ObjectHolder, MemoryObjectLT> MemoryMap;
  
  class AddressSpace {
    friend class ObjectState;
    friend class ExecutionState;
  private:
    typedef std::vector<AddressSpace*> cow_domain_t;

    /// Epoch counter used to control ownership of objects.
    mutable unsigned cowKey;

    mutable cow_domain_t *cowDomain;

    /// Unsupported, use copy constructor
    AddressSpace &operator=(const AddressSpace&); 
    
  public:
    /// The MemoryObject -> ObjectState map that constitutes the
    /// address space.
    ///
    /// The set of objects where o->copyOnWriteOwner == cowKey are the
    /// objects that we own.
    ///
    /// \invariant forall o in objects, o->copyOnWriteOwner <= cowKey
    MemoryMap objects;
    
  public:
    AddressSpace() : cowKey(1), cowDomain(NULL) {}
    AddressSpace(const AddressSpace &b) : cowKey(b.cowKey), cowDomain(NULL), objects(b.objects)  { }
    ~AddressSpace() {}

    /// Resolve address to an ObjectPair in result.
    /// \return true iff an object was found.
    bool resolveOne(const ref<ConstantExpr> &address, 
                    ObjectPair &result);

    /// Resolve address to an ObjectPair in result.
    ///
    /// \param state The state this address space is part of.
    /// \param solver A solver used to determine possible 
    ///               locations of the \a address.
    /// \param address The address to search for.
    /// \param[out] result An ObjectPair this address can resolve to 
    ///               (when returning true).
    /// \return true iff an object was found at \a address.
    bool resolveOne(ExecutionState &state, 
                    TimingSolver *solver,
                    ref<Expr> address,
                    ObjectPair &result,
                    bool &success);

    /// Resolve address to a list of ObjectPairs it can point to. If
    /// maxResolutions is non-zero then no more than that many pairs
    /// will be returned. 
    ///
    /// \return true iff the resolution is incomplete (maxResolutions
    /// is non-zero and the search terminated early, or a query timed out).
    bool resolve(ExecutionState &state,
                 TimingSolver *solver,
                 ref<Expr> address, 
                 ResolutionList &rl, 
                 unsigned maxResolutions=0,
                 double timeout=0.);

    /***/

    /// Add a binding to the address space.
    void bindObject(const MemoryObject *mo, ObjectState *os);

    void bindSharedObject(const MemoryObject *mo, ObjectState *os);

    /// Remove a binding from the address space.
    void unbindObject(const MemoryObject *mo);

    /// Lookup a binding from a MemoryObject.
    const ObjectState *findObject(const MemoryObject *mo) const;

    /// \brief Obtain an ObjectState suitable for writing.
    ///
    /// This returns a writeable object state, creating a new copy of
    /// the given ObjectState if necessary. If the address space owns
    /// the ObjectState then this routine effectively just strips the
    /// const qualifier it.
    ///
    /// \param mo The MemoryObject to get a writeable ObjectState for.
    /// \param os The current binding of the MemoryObject.
    /// \return A writeable ObjectState (\a os or a copy).
    ObjectState *getWriteable(const MemoryObject *mo, const ObjectState *os);

    /// Copy the concrete values of all managed ObjectStates into the
    /// actual system memory location they were allocated at.
    void copyOutConcretes(AddressPool *pool);

    /// Copy the concrete values of all managed ObjectStates back from
    /// the actual system memory location they were allocated
    /// at. ObjectStates will only be written to (and thus,
    /// potentially copied) if the memory values are different from
    /// the current concrete values.
    ///
    /// \retval true The copy succeeded. 
    /// \retval false The copy failed because a read-only object was modified.
    bool copyInConcretes(AddressPool *pool);

    void DumpMemoryObject(std::ostream &os, const MemoryObject *mo,
                          const ObjectState *ostate, bool fast) const;
    void DumpContents(std::ostream &os) const;
  };
} // End klee namespace

#endif
