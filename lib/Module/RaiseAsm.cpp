//===-- RaiseAsm.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "llvm/InlineAsm.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Host.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;
using namespace klee;

char RaiseAsmPass::ID = 0;

Function *RaiseAsmPass::getIntrinsic(llvm::Module &M,
                                     unsigned IID,
                                     Type **Tys,
                                     unsigned NumTys) {  
  return Intrinsic::getDeclaration(&M, (llvm::Intrinsic::ID) IID,
      ArrayRef<Type*>(Tys, NumTys));
}

// FIXME: This should just be implemented as a patch to
// X86TargetAsmInfo.cpp, then everyone will benefit.
bool RaiseAsmPass::runOnInstruction(Module &M, Instruction *I) {
  if (CallInst *ci = dyn_cast<CallInst>(I)) {
    if (InlineAsm *ia = dyn_cast<InlineAsm>(ci->getCalledValue())) {
      //Added verification for empty assemply line
      //Used as memory barrier in LLVM, not needed in Klee
      const std::string &as = ia->getAsmString();
      if(as.length() == 0){
        I->eraseFromParent();
        return true;
      }  
      (void) ia;
      return TLI && TLI->ExpandInlineAsm(ci);
    }
  }

  return false;
}

bool RaiseAsmPass::runOnModule(Module &M) {
  bool changed = false;
  
  std::string Err;
  std::string TargetTriple = llvm::sys::getDefaultTargetTriple();
  std::string CPU = llvm::sys::getHostCPUName();
  const Target *NativeTarget = TargetRegistry::lookupTarget(TargetTriple, Err);
  if (NativeTarget == 0) {
    llvm::errs() << "Warning: unable to select native target: " << Err << "\n";
    TLI = 0;
  } else {
    TargetMachine *TM = NativeTarget->createTargetMachine(
        TargetTriple, CPU, "", TargetOptions());
    TLI = TM->getTargetLowering();
  }

  for (Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
    for (Function::iterator bi = fi->begin(), be = fi->end(); bi != be; ++bi) {
      for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie;) {
        Instruction *i = ii;
        ++ii;  
        changed |= runOnInstruction(M, i);
      }
    }
  }

  return changed;
}
