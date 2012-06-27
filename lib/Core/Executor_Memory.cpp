//===-- Executor_Memory.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "Memory.h"
#include "MemoryManager.h"
#include "TimingSolver.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Support/CommandLine.h"

#include <glog/logging.h>

#include <sstream>

using namespace llvm;

namespace {

cl::opt<bool>
SimplifySymIndices("simplify-sym-indices",
                   cl::init(false));

cl::opt<unsigned>
MaxSymArraySize("max-sym-array-size",
                cl::init(0));
}


namespace klee {

void Executor::bindLocal(KInstruction *target, ExecutionState &state,
                         ref<Expr> value) {
  getDestCell(state, target).value = value;
}

void Executor::bindArgument(KFunction *kf, unsigned index,
                            ExecutionState &state, ref<Expr> value) {
  getArgumentCell(state, kf, index).value = value;
}


void Executor::bindArgumentToPthreadCreate(KFunction *kf, unsigned index,
             StackFrame &sf, ref<Expr> value) {
  getArgumentCell(sf, kf, index).value = value;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state,
                                         const MemoryObject *mo,
                                         bool isLocal,
                                         const Array *array) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);
  state.addressSpace().bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal)
    state.stack().back().allocas.push_back(mo);

  return os;
}

void Executor::resolveExact(ExecutionState &state,
                            ref<Expr> p,
                            ExactResolutionList &results,
                            const std::string &name) {
  // XXX we may want to be capping this?
  ResolutionList rl;
  state.addressSpace().resolve(state, solver, p, rl);

  ExecutionState *unbound = &state;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end();
       it != ie; ++it) {
    ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());

    StatePair branches = fork(*unbound, inBounds, true, KLEE_FORK_INTERNAL);

    if (branches.first)
      results.push_back(std::make_pair(*it, branches.first));

    unbound = branches.second;
    if (!unbound) // Fork failure
      break;
  }

  if (unbound) {
    terminateStateOnError(*unbound,
                          "memory error: invalid pointer: " + name,
                          "ptr.err",
                          getAddressInfo(*unbound, p));
  }
}

KFunction* Executor::resolveFunction(ref<Expr> address)
{
  for (std::vector<KFunction*>::iterator fi = kmodule->functions.begin();
   fi != kmodule->functions.end(); fi++) {
    KFunction* f = (*fi);
    ref<Expr> addr = Expr::createPointer((uint64_t) (void*) f->function);
    if(addr == address)
      return f;
  }
  return NULL;
}

void Executor::executeAlloc(ExecutionState &state,
                            ref<Expr> size,
                            bool isLocal,
                            KInstruction *target,
                            bool zeroMemory,
                            const ObjectState *reallocFrom) {

  size = toUnique(state, size);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
    MemoryObject *mo = memory->allocate(&state, CE->getZExtValue(), isLocal, false,
                                        state.prevPC()->inst);
    if (!mo) {
      bindLocal(target, state,
                ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      ObjectState *os = bindObjectInState(state, mo, isLocal);
      if (zeroMemory) {
        os->initializeToZero();
      } else {
        os->initializeToRandom();
      }
      bindLocal(target, state, mo->getBaseExpr());

      if (reallocFrom) {
        unsigned count = std::min(reallocFrom->size, os->size);
        for (unsigned i=0; i<count; i++)
          os->write(i, reallocFrom->read8(i));
        state.addressSpace().unbindObject(reallocFrom->getObject());
      }
    }

    return;
  }

  // XXX For now we just pick a size. Ideally we would support
  // symbolic sizes fully but even if we don't it would be better to
  // "smartly" pick a value, for example we could fork and pick the
  // min and max values and perhaps some intermediate (reasonable
  // value).
  //
  // It would also be nice to recognize the case when size has
  // exactly two values and just fork (but we need to get rid of
  // return argument first). This shows up in pcre when llvm
  // collapses the size expression with a select.

  ref<ConstantExpr> example;

  bool success = solver->getValue(data::ALLOC_RANGE_CHECK, state, size, example);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;

  // Try and start with a small example.
  Expr::Width W = example->getWidth();
  while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
    ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
    bool res;
    bool success = solver->mayBeTrue(data::ALLOC_RANGE_CHECK, state, EqExpr::create(tmp, size), res);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    if (!res)
      break;
    example = tmp;
  }

  StatePair fixedSize = fork(state, EqExpr::create(example, size), true, KLEE_FORK_INTERNAL);

  if (fixedSize.second) {
    // Check for exactly two values
    ref<ConstantExpr> tmp;
    bool success = solver->getValue(data::ALLOC_RANGE_CHECK, *fixedSize.second, size, tmp);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bool res;
    success = solver->mustBeTrue(data::ALLOC_RANGE_CHECK, *fixedSize.second,
                                 EqExpr::create(tmp, size),
                                 res);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    if (res) {
      executeAlloc(*fixedSize.second, tmp, isLocal,
                   target, zeroMemory, reallocFrom);
    } else {
      // See if a *really* big value is possible. If so assume
      // malloc will fail for it, so lets fork and return 0.
      Expr::Width W = example->getWidth();
      StatePair hugeSize =
        fork(*fixedSize.second,
             UltExpr::create(ConstantExpr::alloc(1<<31, W), size),
             true, KLEE_FORK_INTERNAL);
      if (hugeSize.first) {
        LOG(INFO) << "Found huge malloc, returing 0";
        bindLocal(target, *hugeSize.first,
                  ConstantExpr::alloc(0, Context::get().getPointerWidth()));
      }

      if (hugeSize.second) {
        std::ostringstream info;
        ExprPPrinter::printOne(info, "  size expr", size);
        info << "  concretization : " << example << "\n";
        info << "  unbound example: " << tmp << "\n";
        terminateStateOnError(*hugeSize.second,
                              "concretized symbolic size",
                              "model.err",
                              info.str());
      }
    }
  }

  if (fixedSize.first) { // can be zero when fork fails
    executeAlloc(*fixedSize.first, example, isLocal,
                 target, zeroMemory, reallocFrom);
  }
}

void Executor::executeFree(ExecutionState &state,
                           ref<Expr> address,
                           KInstruction *target) {
  StatePair zeroPointer = fork(state, Expr::createIsZero(address), true, KLEE_FORK_INTERNAL);
  if (zeroPointer.first) {
    if (target)
      bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
  }
  if (zeroPointer.second) { // address != 0
    ExactResolutionList rl;
    resolveExact(*zeroPointer.second, address, rl, "free");

    for (Executor::ExactResolutionList::iterator it = rl.begin(),
           ie = rl.end(); it != ie; ++it) {
      const MemoryObject *mo = it->first.first;
      if (mo->isLocal) {
        terminateStateOnError(*it->second,
                              "free of alloca",
                              "free.err",
                              getAddressInfo(*it->second, address));
      } else if (mo->isGlobal) {
        terminateStateOnError(*it->second,
                              "free of global",
                              "free.err",
                              getAddressInfo(*it->second, address));
      } else {
        it->second->addressSpace().unbindObject(mo);
        if (target)
          bindLocal(target, *it->second, Expr::createPointer(0));
      }
    }
  }
}

void Executor::executeMemoryOperation(ExecutionState &state,
                                      bool isWrite,
                                      ref<Expr> address,
                                      ref<Expr> value /* undef if read */,
                                      KInstruction *target /* undef if write */) {
  Expr::Width type = (isWrite ? value->getWidth() :
                     getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);

  if (SimplifySymIndices) {
    if (!isa<ConstantExpr>(address))
      address = state.constraints().simplifyExpr(address);
    if (isWrite && !isa<ConstantExpr>(value))
      value = state.constraints().simplifyExpr(value);
  }

  ObjectPair op;
  bool resolved = false;

  // fast path: single in-bounds resolution
  solver->setTimeout(stpTimeout);
  if (!state.addressSpace().resolveOne(state, solver, address, op, resolved)) {
    address = toConstant(state, address, "resolveOne failure");
    resolved = state.addressSpace().resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(0);

  if (resolved) {
    const MemoryObject *mo = op.first;

    if (MaxSymArraySize && mo->size>=MaxSymArraySize) {
      address = toConstant(state, address, "max-sym-array-size");
    }

    ref<Expr> offset = mo->getOffsetExpr(address);

    solver->setTimeout(stpTimeout);
    bool success = solver->mustBeTrue(data::SINGLE_ADDRESS_RESOLUTION, state,
                                      mo->getBoundsCheckOffset(offset, bytes),
                                      resolved);
    solver->setTimeout(0);
    if (!success) {
      state.pc() = state.prevPC();
      terminateStateEarly(state, "query timed out");
      return;
    }
  }

  if (resolved) {
    const MemoryObject *mo = op.first;
    const ObjectState *os = op.second;
    ref<Expr> offset = mo->getOffsetExpr(address);

    if (isWrite) {
      if (os->readOnly) {
        terminateStateOnError(state,
                              "memory error: object read only",
                              "readonly.err");
      } else {
        ObjectState *wos = state.addressSpace().getWriteable(mo, os);
        wos->write(offset, value);
      }
    } else {
      ref<Expr> result = os->read(offset, type);

      if (interpreterOpts.MakeConcreteSymbolic)
        result = replaceReadWithSymbolic(state, result);

      bindLocal(target, state, result);
    }

    return;
  }

  // we are on an error path (no resolution, multiple resolution, one
  // resolution with out of bounds)

  ResolutionList rl;
  solver->setTimeout(stpTimeout);
  bool incomplete = state.addressSpace().resolve(state, solver, address, rl,
                                               0, stpTimeout);
  solver->setTimeout(0);

  // XXX there is some query wasteage here. who cares?
  ExecutionState *unbound = &state;

  for (ResolutionList::iterator i = rl.begin(), ie = rl.end(); i != ie; ++i) {
    const MemoryObject *mo = i->first;
    const ObjectState *os = i->second;
    ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

    StatePair branches = fork(*unbound, inBounds, true, KLEE_FORK_INTERNAL);
    ExecutionState *bound = branches.first;

    // bound can be 0 on failure or overlapped
    if (bound) {
      if (isWrite) {
        if (os->readOnly) {
          terminateStateOnError(*bound,
                                "memory error: object read only",
                                "readonly.err");
        } else {
          ObjectState *wos = bound->addressSpace().getWriteable(mo, os);
          wos->write(mo->getOffsetExpr(address), value);
        }
      } else {
        ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
        bindLocal(target, *bound, result);
      }
    }

    unbound = branches.second;
    if (!unbound)
      break;
  }

  // XXX should we distinguish out of bounds and overlapped cases?
  if (unbound) {
    if (incomplete) {
      terminateStateEarly(*unbound, "query timed out (resolve)");
    } else {
      terminateStateOnError(*unbound,
                            "memory error: out of bound pointer",
                            "ptr.err",
                            getAddressInfo(*unbound, address));
    }
  }
}


void Executor::executeMakeSymbolic(ExecutionState &state,
                                   const MemoryObject *mo,
                                   bool shared) {

  // TODO(sbucur): Memory object may not be the same across states

  // First, create a new symbolic array
  static unsigned id = 0;
  const Array *array = new Array("arr" + llvm::utostr(++id),
                                 mo->size);

	ObjectState *os = bindObjectInState(state, mo, false, array);
	os->isShared = shared;
	state.addSymbolic(mo, array);
}

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  e = state.constraints().simplifyExpr(e);
  ref<ConstantExpr> value;
  bool success = solver->getValue(data::USER_GET_VALUE, state, e, value);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;
  bindLocal(target, state, value);
  return;
}

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state,
                                            ref<Expr> e) {
  unsigned n = interpreterOpts.MakeConcreteSymbolic;
  if (!n)
    return e;

  // right now, we don't replace symbolics (is there any reason too?)
  if (!isa<ConstantExpr>(e))
    return e;

  if (n != 1 && random() %  n)
    return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.

  static unsigned id;
  const Array *array = new Array("rrws_arr" + llvm::utostr(++id),
                                 Expr::getMinBytesForWidth(e->getWidth()));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  std::cerr << "Making symbolic: " << eq << "\n";
  state.addConstraint(eq);
  return res;
}

}
