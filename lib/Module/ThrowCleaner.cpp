/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#include "Passes.h"

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

#include <glog/logging.h>

using namespace llvm;

namespace klee {

char ThrowCleanerPass::ID;

static void logTransformation(Instruction *I) {
  if (VLOG_IS_ON(10)) {
    std::string istr;
    llvm::raw_string_ostream rso(istr);
    rso << *I;
    rso.flush();

    VLOG(10) << "Throw cleaner hit instruction: " << istr;
  }
}

bool ThrowCleanerPass::runOnModule(Module &M) {
  Function *throwFn = M.getFunction("__cxa_throw");

  if (!throwFn || throwFn->use_empty()) {
    LOG(INFO) << "No exceptions thrown in the code.";
    // We assume no exceptions are thrown in this code. Yupee!
    return false;
  }

  Function *exitFn = M.getFunction("_exit");
  assert(exitFn && "uhoh...");

  std::vector<Value*> throwUses;

  for (Function::use_iterator U = throwFn->use_begin(), UE = throwFn->use_end();
      U != UE; U++) {
    Value *V = *U;
    throwUses.push_back(V);
  }

  for (std::vector<Value*>::iterator it = throwUses.begin(), ie = throwUses.end();
      it != ie; ++it) {
    Value *V = *it;
    if (InvokeInst *II = dyn_cast<InvokeInst>(V)) {
      assert(II->getCalledFunction() == throwFn);

      // Fix the successors
      for (unsigned i = 0; i < II->getNumSuccessors(); i++) {
        BasicBlock *succBB = II->getSuccessor(i);
        succBB->removePredecessor(II->getParent());
      }
    } else if (CallInst *CI = dyn_cast<CallInst>(V)) {
      assert(CI->getCalledFunction() == throwFn);
    } else {
      assert(0 && "unexpected use of __cxa_throw");
    }

    Instruction *I = dyn_cast<Instruction>(V);
    BasicBlock *BB = I->getParent();
    logTransformation(I);

    IRBuilder<> Builder(BB, I);
    CallInst *NewCI = Builder.CreateCall(exitFn,
        ConstantInt::get(Type::getInt32Ty(getGlobalContext()), 1));

    if (BB->getTerminator() == I)
      Builder.CreateUnreachable();

    NewCI->setName(I->getName());
    if (!I->use_empty())
        I->replaceAllUsesWith(NewCI);
    I->eraseFromParent();
  }

  return true;
}

}
