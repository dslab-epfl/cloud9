//===-- Executor_Calls.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "Context.h"
#include "ExternalDispatcher.h"
#include "StatsTracker.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "SpecialFunctionHandler.h"
#include "TimingSolver.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <glog/logging.h>

#include <set>
#include <sstream>

using namespace llvm;

namespace {

cl::opt<bool>
AllowExternalSymCalls("allow-external-sym-calls",
                      cl::init(false));

cl::opt<bool>
DebugCallHistory("debug-call-history", cl::init(false));

cl::opt<bool>
NoExternals("no-externals",
         cl::desc("Do not allow external functin calls"));

cl::opt<bool>
SuppressExternalWarnings("suppress-external-warnings");

}

namespace klee {

// XXX shoot me
static const char *okExternalsList[] = { "printf",
                                         "fprintf",
                                         "puts",
                                         "getpid" };
static std::set<std::string> okExternals(okExternalsList,
                                         okExternalsList +
                                         (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void Executor::executeCall(ExecutionState &state,
                           KInstruction *ki,
                           Function *f,
                           std::vector< ref<Expr> > &arguments) {
  fireControlFlowEvent(&state, ::cloud9::worker::CALL);

  if (f && DebugCallHistory) {
    unsigned depth = state.stack().size();

    LOG(INFO) << "Call[" << &state << "]: " << std::string(depth, ' ') << f->getName().str();
  }

  Instruction *i = NULL;
  if (ki)
      i = ki->inst;

  if (ki && f && f->isDeclaration()) {
    switch(f->getIntrinsicID()) {
    case Intrinsic::not_intrinsic:
      // state may be destroyed by this call, cannot touch
      callExternalFunction(state, ki, f, arguments);
      break;

      // va_arg is handled by caller and intrinsic lowering, see comment for
      // ExecutionState::varargs
    case Intrinsic::vastart:  {
      StackFrame &sf = state.stack().back();
      assert(sf.varargs &&
             "vastart called in function with no vararg object");

      // FIXME: This is really specific to the architecture, not the pointer
      // size. This happens to work fir x86-32 and x86-64, however.
      Expr::Width WordSize = Context::get().getPointerWidth();
      if (WordSize == Expr::Int32) {
        executeMemoryOperation(state, true, arguments[0],
                               sf.varargs->getBaseExpr(), 0);
      } else {
        assert(WordSize == Expr::Int64 && "Unknown word size!");

        // X86-64 has quite complicated calling convention. However,
        // instead of implementing it, we can do a simple hack: just
        // make a function believe that all varargs are on stack.
        executeMemoryOperation(state, true, arguments[0],
                               ConstantExpr::create(48, 32), 0); // gp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0],
                                               ConstantExpr::create(4, 64)),
                               ConstantExpr::create(304, 32), 0); // fp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0],
                                               ConstantExpr::create(8, 64)),
                               sf.varargs->getBaseExpr(), 0); // overflow_arg_area
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0],
                                               ConstantExpr::create(16, 64)),
                               ConstantExpr::create(0, 64), 0); // reg_save_area
      }
      break;
    }
    case Intrinsic::vaend:
      // va_end is a noop for the interpreter.
      //
      // FIXME: We should validate that the target didn't do something bad
      // with vaeend, however (like call it twice).
      break;

    case Intrinsic::vacopy:
      // va_copy should have been lowered.
      //
      // FIXME: It would be nice to check for errors in the usage of this as
      // well.
    default:
      LOG(FATAL) << "Unknown intrinsic: " << f->getName().data();
    }

    if (InvokeInst *ii = dyn_cast<InvokeInst>(i))
      transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
  } else {
    // FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
    // guess. This just done to avoid having to pass KInstIterator everywhere
    // instead of the actual instruction, since we can't make a KInstIterator
    // from just an instruction (unlike LLVM).
    KFunction *kf = kmodule->functionMap[f];
    state.pushFrame(state.prevPC(), kf);
    state.pc() = kf->instructions;

    if (statsTracker)
      statsTracker->framePushed(state, &state.stack()[state.stack().size()-2]); //XXX TODO fix this ugly stuff

     // TODO: support "byval" parameter attribute
     // TODO: support zeroext, signext, sret attributes

    unsigned callingArgs = arguments.size();
    unsigned funcArgs = f->arg_size();
    if (!f->isVarArg()) {
      if (callingArgs > funcArgs) {
        LOG(WARNING) << "Calling " << f->getName().data() << " with extra arguments.";
      } else if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              "user.err");
        return;
      }
    } else {
      if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              "user.err");
        return;
      }

      StackFrame &sf = state.stack().back();
      unsigned size = 0;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work fir x86-32 and x86-64, however.
        Expr::Width WordSize = Context::get().getPointerWidth();
        if (WordSize == Expr::Int32) {
          size += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          size += llvm::RoundUpToAlignment(arguments[i]->getWidth(),
                                           WordSize) / 8;
        }
      }

      MemoryObject *mo = sf.varargs = memory->allocate(&state, size, true, false,
                                                       state.prevPC()->inst);
      if (!mo) {
        terminateStateOnExecError(state, "out of memory (varargs)");
        return;
      }
      ObjectState *os = bindObjectInState(state, mo, true);
      unsigned offset = 0;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work fir x86-32 and x86-64, however.
        Expr::Width WordSize = Context::get().getPointerWidth();
        if (WordSize == Expr::Int32) {
          os->write(offset, arguments[i]);
          offset += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          assert(WordSize == Expr::Int64 && "Unknown word size!");
          os->write(offset, arguments[i]);
          offset += llvm::RoundUpToAlignment(arguments[i]->getWidth(),
                                             WordSize) / 8;
        }
      }
    }

    unsigned numFormals = f->arg_size();
    for (unsigned i=0; i<numFormals; ++i)
      bindArgument(kf, i, state, arguments[i]);
  }
}

Function* Executor::getCalledFunction(CallSite &cs, ExecutionState &state) {
  Function *f = cs.getCalledFunction();

  if (f) {
    std::string alias = state.getFnAlias(f->getName());
    if (alias != "") {
      llvm::Module* currModule = kmodule->module;
      Function* old_f = f;
      f = currModule->getFunction(alias);
      if (!f) {
  llvm::errs() << "Function " << alias << "(), alias for "
                     << old_f->getName().str() << " not found!\n";
  assert(f && "function alias not found");
      }
    }
  }

  return f;
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src,
                                    ExecutionState &state) {
  // Note that in general phi nodes can reuse phi values from the same
  // block but the incoming value is the eval() result *before* the
  // execution of any phi nodes. this is pathological and doesn't
  // really seem to occur, but just in case we run the PhiCleanerPass
  // which makes sure this cannot happen and so it is safe to just
  // eval things in order. The PhiCleanerPass also makes sure that all
  // incoming blocks have the same order for each PHINode so we only
  // have to compute the index once.
  //
  // With that done we simply set an index in the state so that PHI
  // instructions know which argument to eval, set the pc, and continue.

  // XXX this lookup has to go ?
  KFunction *kf = state.stack().back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  state.pc() = &kf->instructions[entry];
  if (state.pc()->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(state.pc()->inst);
    state.crtThread().incomingBBIndex = first->getBasicBlockIndex(src);
  }
}

void Executor::callExternalFunction(ExecutionState &state,
                                    KInstruction *target,
                                    Function *function,
                                    std::vector< ref<Expr> > &arguments) {
  // check if specialFunctionHandler wants it
  if (specialFunctionHandler->handle(state, function, target, arguments))
    return;

  callUnmodelledFunction(state, target, function, arguments);
}

void Executor::callUnmodelledFunction(ExecutionState &state,
                            KInstruction *target,
                            llvm::Function *function,
                            std::vector<ref<Expr> > &arguments) {

  if (NoExternals && !okExternals.count(function->getName())) {
    std::cerr << "KLEE:ERROR: Calling not-OK external function : "
               << function->getName().str() << "\n";
    terminateStateOnError(state, "externals disallowed", "user.err");
    return;
  }

  // normal external function handling path
  // allocate 128 bits for each argument (+return value) to support fp80's;
  // we could iterate through all the arguments first and determine the exact
  // size we need, but this is faster, and the memory usage isn't significant.
  uint64_t *args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
  memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
  unsigned wordIndex = 2;
  for (std::vector<ref<Expr> >::iterator ai = arguments.begin(),
      ae = arguments.end(); ai!=ae; ++ai) {
    if (AllowExternalSymCalls) { // don't bother checking uniqueness
      ref<ConstantExpr> ce;
      bool success = solver->getValue(data::EXTERNAL_CALL_CONCRETIZATION, state, *ai, ce);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      ce->toMemory(&args[wordIndex]);
      wordIndex += (ce->getWidth()+63)/64;
    } else {
      ref<Expr> arg = toUnique(state, *ai);
      if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
        // XXX kick toMemory functions from here
        ce->toMemory(&args[wordIndex]);
        wordIndex += (ce->getWidth()+63)/64;
      } else {
        terminateStateOnExecError(state,
                                  "external call with symbolic argument: " +
                                  function->getName());
        return;
      }
    }
  }

  state.addressSpace().copyOutConcretes(&state.addressPool);

  if (!SuppressExternalWarnings) {
    StackTrace stack_trace = state.getStackTrace();

    std::ostringstream os;
    os << state <<
        " Calling external: " << function->getName().str() << "(";
    for (unsigned i=0; i<arguments.size(); i++) {
      os << arguments[i];
      if (i != arguments.size()-1)
    os << ", ";
    }
    os << ")";

    if (state.isExternalCallSafe())
      VLOG(1) << os.str().c_str();
    else
      LOG(INFO) << os.str().c_str();
  }

  bool success = externalDispatcher->executeCall(function, target->inst, args);
  if (!success) {
    terminateStateOnError(state, "failed external call: " + function->getName(),
                          "external.err");
    return;
  }

  if (!state.addressSpace().copyInConcretes(&state.addressPool)) {
    terminateStateOnError(state, "external modified read-only object",
                          "external.err");
    return;
  }

  Type *resultType = target->inst->getType();
  if (resultType != Type::getVoidTy(getGlobalContext())) {
    ref<Expr> e = ConstantExpr::fromMemory((void*) args,
                                           getWidthForLLVMType(resultType));
    bindLocal(target, state, e);
  }
}

}
