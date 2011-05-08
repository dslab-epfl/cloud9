//===-- SpecialFunctionHandler.h --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SPECIALFUNCTIONHANDLER_H
#define KLEE_SPECIALFUNCTIONHANDLER_H

#include <map>
#include <vector>
#include <string>

namespace llvm {
  class Function;
}

namespace klee {
  class Executor;
  class Expr;
  class ExecutionState;
  struct KInstruction;
  template<typename T> class ref;
  
  class SpecialFunctionHandler {
  public:
    typedef void (SpecialFunctionHandler::*Handler)(ExecutionState &state,
                                                    KInstruction *target, 
                                                    std::vector<ref<Expr> > 
                                                      &arguments);
    typedef std::map<const llvm::Function*, 
                     std::pair<Handler,bool> > handlers_ty;
    typedef std::vector< std::pair<std::pair<const MemoryObject*, const ObjectState*>,
        ExecutionState*> > resolutions_ty;

    handlers_ty handlers;
    class Executor &executor;

  public:
    SpecialFunctionHandler(Executor &_executor);

    /// Perform any modifications on the LLVM module before it is
    /// prepared for execution. At the moment this involves deleting
    /// unused function bodies and marking intrinsics with appropriate
    /// flags for use in optimizations.
    void prepare();

    /// Initialize the internal handler map after the module has been
    /// prepared for execution.
    void bind();

    bool handle(ExecutionState &state, 
                llvm::Function *f,
                KInstruction *target,
                std::vector< ref<Expr> > &arguments);

    /* Convenience routines */

    void processMemoryLocation(ExecutionState &state,
        ref<Expr> address, ref<Expr> size,
        const std::string &name, resolutions_ty &resList);

    bool writeConcreteValue(ExecutionState &state,
        ref<Expr> address, uint64_t value, Expr::Width width);

    std::string readStringAtAddress(ExecutionState &state, ref<Expr> address);
    
    /* Handlers */

#define HANDLER(name) void name(ExecutionState &state, \
                                KInstruction *target, \
                                std::vector< ref<Expr> > &arguments)

    HANDLER(handleAbort);
    HANDLER(handleAliasFunction);
    HANDLER(handleAssert);
    HANDLER(handleAssertFail);
    HANDLER(handleAssume);
    HANDLER(handleBranch);
    HANDLER(handleCalloc);
    HANDLER(handleCheckMemoryAccess);
    HANDLER(handleDebug);
    HANDLER(handleDefineFixedObject);
    HANDLER(handleDelete);    
    HANDLER(handleDeleteArray);
    HANDLER(handleEvent);
    HANDLER(handleFork);
    HANDLER(handleFree);
    HANDLER(handleGetContext);
    HANDLER(handleGetErrno);
    HANDLER(handleGetObjSize);
    HANDLER(handleGetTime);
    HANDLER(handleGetValue);
    HANDLER(handleGetWList);
    HANDLER(handleIsSymbolic);
    HANDLER(handleMakeShared);
    HANDLER(handleMakeSymbolic);
    HANDLER(handleMalloc);
    HANDLER(handleMarkGlobal);
    HANDLER(handleMerge);
    HANDLER(handleNew);
    HANDLER(handleNewArray);
    HANDLER(handlePreferCex);
    HANDLER(handlePrintExpr);
    HANDLER(handlePrintRange);
    HANDLER(handleProcessFork);
    HANDLER(handleProcessTerminate);
    HANDLER(handleRange);
    HANDLER(handleRealloc);
    HANDLER(handleReportError);
    HANDLER(handleRevirtObjects);
    HANDLER(handleSetForking);
    HANDLER(handleSetTime);
    HANDLER(handleSilentExit);
    HANDLER(handleStackTrace);
    HANDLER(handleSyscall);
    HANDLER(handleThreadCreate);
    HANDLER(handleThreadNotify);
    HANDLER(handleThreadPreempt);
    HANDLER(handleThreadSleep);
    HANDLER(handleThreadTerminate);
    HANDLER(handleUnderConstrained);
    HANDLER(handleValloc);
    HANDLER(handleWarning);
    HANDLER(handleWarningOnce);
#undef HANDLER
  };
} // End klee namespace

#endif
