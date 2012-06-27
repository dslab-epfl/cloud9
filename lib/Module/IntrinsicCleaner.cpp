//===-- IntrinsicCleaner.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "klee/Config/config.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Target/TargetData.h"
#include "llvm/ADT/ArrayRef.h"

#include <glog/logging.h>

using namespace llvm;

namespace klee {

char IntrinsicCleanerPass::ID;

/// ReplaceCallWith - This function is used when we want to lower an intrinsic
/// call to a call of an external function.  This handles hard cases such as
/// when there was already a prototype for the external function, and if that
/// prototype doesn't match the arguments we expect to pass in.
template <class ArgIt>
static CallInst *ReplaceCallWith(const char *NewFn, CallInst *CI,
                                 ArgIt ArgBegin, ArgIt ArgEnd,
                                 Type *RetTy) {
  // If we haven't already looked up this function, check to see if the
  // program already contains a function with this name.
  Module *M = CI->getParent()->getParent()->getParent();
  // Get or insert the definition now.
  std::vector<Type *> ParamTys;
  for (ArgIt I = ArgBegin; I != ArgEnd; ++I)
    ParamTys.push_back((*I)->getType());
  Constant* FCache = M->getOrInsertFunction(NewFn,
                                  FunctionType::get(RetTy, ParamTys, false));

  IRBuilder<> Builder(CI->getParent(), CI);
  SmallVector<Value *, 8> Args(ArgBegin, ArgEnd);
  CallInst *NewCI = Builder.CreateCall(FCache, ArrayRef<Value*>(Args));
  NewCI->setName(CI->getName());
  if (!CI->use_empty())
    CI->replaceAllUsesWith(NewCI);
  CI->eraseFromParent();
  return NewCI;
}

static void ReplaceFPIntrinsicWithCall(CallInst *CI, const char *Fname,
                                       const char *Dname,
                                       const char *LDname) {
  CallSite CS(CI);
  switch (CI->getArgOperand(0)->getType()->getTypeID()) {
  default: LOG(FATAL) << "Invalid type in intrinsic";
  case Type::FloatTyID:
    ReplaceCallWith(Fname, CI, CS.arg_begin(), CS.arg_end(),
                  Type::getFloatTy(CI->getContext()));
    break;
  case Type::DoubleTyID:
    ReplaceCallWith(Dname, CI, CS.arg_begin(), CS.arg_end(),
                  Type::getDoubleTy(CI->getContext()));
    break;
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
    ReplaceCallWith(LDname, CI, CS.arg_begin(), CS.arg_end(),
                  CI->getArgOperand(0)->getType());
    break;
  }
}

static void ReplaceIntIntrinsicWithCall(CallInst *CI, const char *Fname,
                                       const char *Dname,
                                       const char *LDname) {
  CallSite CS(CI);
  switch (CI->getArgOperand(0)->getType()->getTypeID()) {
  default: LOG(FATAL) << "Invalid type in intrinsic";
  case Type::IntegerTyID:
    IntegerType* itype = (IntegerType*)CI->getArgOperand(0)->getType();
    IntegerType* btype = IntegerType::get(CI->getContext(), 1);
    switch(itype->getBitWidth()) {
      case 16:
        ReplaceCallWith(Fname, CI, CS.arg_begin(), CS.arg_end(),
                  StructType::get(itype, btype, NULL));
        break;
      case 32:
        ReplaceCallWith(Dname, CI, CS.arg_begin(), CS.arg_end(),
                  StructType::get(itype, btype, NULL));
        break;
      case 64:
        ReplaceCallWith(LDname, CI, CS.arg_begin(), CS.arg_end(),
                  StructType::get(itype, btype, NULL));
        break;
    }
    break;
  }
}

bool IntrinsicCleanerPass::runOnModule(Module &M) {
  bool dirty = false;
  for (Module::iterator f = M.begin(), fe = M.end(); f != fe; ++f)
    for (Function::iterator b = f->begin(), be = f->end(); b != be;)
      dirty |= runOnBasicBlock(*(b++));
  return dirty;
}

bool IntrinsicCleanerPass::runOnBasicBlock(BasicBlock &b) { 
  bool dirty = false;
  
  unsigned WordSize = TargetData.getPointerSizeInBits() / 8;
  for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie;) {     
    IntrinsicInst *ii = dyn_cast<IntrinsicInst>(&*i);
    // increment now since LowerIntrinsic deletion makes iterator invalid.
    ++i;  
    if(ii) {
      CallSite CS(ii);
      switch (ii->getIntrinsicID()) {
      case Intrinsic::vastart:
      case Intrinsic::vaend:
        break;
        
        // Lower vacopy so that object resolution etc is handled by
        // normal instructions.
        //
        // FIXME: This is much more target dependent than just the word size,
        // however this works for x86-32 and x86-64.
      case Intrinsic::vacopy: { // (dst, src) -> *((i8**) dst) = *((i8**) src)
        Value *dst = ii->getArgOperand(0);
        Value *src = ii->getArgOperand(1);

        if (WordSize == 4) {
          Type *i8pp = PointerType::getUnqual(PointerType::getUnqual(Type::getInt8Ty(getGlobalContext())));
          Value *castedDst = CastInst::CreatePointerCast(dst, i8pp, "vacopy.cast.dst", ii);
          Value *castedSrc = CastInst::CreatePointerCast(src, i8pp, "vacopy.cast.src", ii);
          Value *load = new LoadInst(castedSrc, "vacopy.read", ii);
          new StoreInst(load, castedDst, false, ii);
        } else {
          assert(WordSize == 8 && "Invalid word size!");
          Type *i64p = PointerType::getUnqual(Type::getInt64Ty(getGlobalContext()));
          Value *pDst = CastInst::CreatePointerCast(dst, i64p, "vacopy.cast.dst", ii);
          Value *pSrc = CastInst::CreatePointerCast(src, i64p, "vacopy.cast.src", ii);
          Value *val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
          Value *off = ConstantInt::get(Type::getInt64Ty(getGlobalContext()), 1);
          pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
          pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
          val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
          pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
          pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
          val = new LoadInst(pSrc, std::string(), ii); new StoreInst(val, pDst, ii);
        }
        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::dbg_value:
      case Intrinsic::dbg_declare:
        // Remove these regardless of lower intrinsics flag. This can
        // be removed once IntrinsicLowering is fixed to not have bad
        // caches.
        ii->eraseFromParent();
        dirty = true;
        break;      
      case Intrinsic::powi:
        ReplaceFPIntrinsicWithCall(ii, "powif", "powi", "powil");
        dirty = true;
        break;
      case Intrinsic::uadd_with_overflow:
        ReplaceIntIntrinsicWithCall(ii, "uadds", "uadd", "uaddl");
        dirty = true;
        break;
      case Intrinsic::umul_with_overflow:
        ReplaceIntIntrinsicWithCall(ii, "umuls", "umul", "umull");
        dirty = true;
        break;
      case Intrinsic::trap:
        // Link with abort
        ReplaceCallWith("abort", ii, CS.arg_end(), CS.arg_end(),
                        Type::getVoidTy(getGlobalContext()));
        dirty = true;
        break;
      case Intrinsic::memset:
      case Intrinsic::memcpy:
      case Intrinsic::memmove: {
        LLVMContext &Ctx = ii->getContext();

        Value *dst = ii->getArgOperand(0);
        Value *src = ii->getArgOperand(1);
        Value *len = ii->getArgOperand(2);

        BasicBlock *BB = ii->getParent();
        Function *F = BB->getParent();

        BasicBlock *exitBB = BB->splitBasicBlock(ii);
        BasicBlock *headerBB = BasicBlock::Create(Ctx, Twine(), F, exitBB);
        BasicBlock *bodyBB  = BasicBlock::Create(Ctx, Twine(), F, exitBB);

        // Enter the loop header
        BB->getTerminator()->eraseFromParent();
        BranchInst::Create(headerBB, BB);

        // Create loop index
        PHINode *idx = PHINode::Create(len->getType(), 1, Twine(), headerBB);
        idx->addIncoming(ConstantInt::get(len->getType(), 0), BB);

        // Check loop condition, then move to the loop body or exit the loop
        Value *loopCond = ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_ULT,
                                           idx, len, Twine(), headerBB);
        BranchInst::Create(bodyBB, exitBB, loopCond, headerBB);

        // Get value to store
        Value *val;
        if (ii->getIntrinsicID() == Intrinsic::memset) {
          val = src;
        } else {
          Value *srcPtr = GetElementPtrInst::Create(src, idx, Twine(), bodyBB);
          val = new LoadInst(srcPtr, Twine(), bodyBB);
        }

        // Store the value
        Value* dstPtr = GetElementPtrInst::Create(dst, idx, Twine(), bodyBB);
        new StoreInst(val, dstPtr, bodyBB);

        // Update index and branch back
        Value* newIdx = BinaryOperator::Create(Instruction::Add,
                    idx, ConstantInt::get(len->getType(), 1), Twine(), bodyBB);
        BranchInst::Create(headerBB, bodyBB);
        idx->addIncoming(newIdx, bodyBB);

        ii->eraseFromParent();

        // Update iterators to continue in the next BB
        i = exitBB->begin();
        ie = exitBB->end();
        break;
      }
      default:
        if (LowerIntrinsics)
          IL->LowerIntrinsicCall(ii);
        dirty = true;
        break;
      }
    }
  }

  return dirty;
}
}
