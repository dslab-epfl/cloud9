//===-- LowerSSE.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "klee/Config/config.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
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
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <sstream>

using namespace llvm;

namespace klee {

char LowerSSEPass::ID;

bool LowerSSEPass::runOnModule(Module &M) {
  bool dirty = false;
  for (Module::iterator f = M.begin(), fe = M.end(); f != fe; ++f)
    for (Function::iterator b = f->begin(), be = f->end(); b != be; ++b)
      dirty |= runOnBasicBlock(*b);
  return dirty;
}

static Value *CreateSaturatedValue(IRBuilder<> &builder, bool isSigned, IntegerType *tt, Value *val) {
  assert(isa<IntegerType>(val->getType()));
  IntegerType *ft = cast<IntegerType>(val->getType());
  APInt maxVal = (isSigned ? APInt::getSignedMaxValue : APInt::getMaxValue)(tt->getBitWidth()).zext(ft->getBitWidth());
  Constant *maxValConst = ConstantInt::get(ft, maxVal);
  Value *cmp = isSigned ? builder.CreateICmpSGT(val, maxValConst) : builder.CreateICmpUGT(val, maxValConst);
  val = builder.CreateSelect(cmp, maxValConst, val);
  if (isSigned) {
    APInt minVal = APInt::getSignedMinValue(tt->getBitWidth()).sext(ft->getBitWidth());
    Constant *minValConst = ConstantInt::get(ft, minVal);
    Value *cmp = isSigned ? builder.CreateICmpSLT(val, minValConst) : builder.CreateICmpULT(val, minValConst);
    val = builder.CreateSelect(cmp, minValConst, val);
  }
  val = builder.CreateTrunc(val, tt);
  return val;
}

static Value *CreateSaturatedUnsignedSub(IRBuilder<> &builder, Value *l, Value *r) {
  assert(l->getType() == r->getType());
  Value *cmp = builder.CreateICmpUGT(l, r);
  Constant *zero = ConstantInt::get(l->getType(), 0);
  Value *sub = builder.CreateSub(l, r);
  Value *result = builder.CreateSelect(cmp, sub, zero);
  return result;
}

static Value *CreateSaturatedUnsignedAdd(IRBuilder<> &builder, Value *l, Value *r) {
  assert(l->getType() == r->getType());
  Value *notl = builder.CreateNot(l);
  Value *cmp = builder.CreateICmpUGT(notl, r);
  Constant *ones = ConstantInt::get(l->getType(), -1);
  Value *add = builder.CreateAdd(l, r);
  Value *result = builder.CreateSelect(cmp, add, ones);
  return result;
}

static Value *CreateIsNegative(IRBuilder<> &builder, Value *v) {
  IntegerType *t = cast<IntegerType>(v->getType());
  return builder.CreateICmpNE(builder.CreateAShr(v, t->getBitWidth()-1), ConstantInt::get(t, 0));
}

static Value *CreateSaturatedSignedAdd(IRBuilder<> &builder, Value *l, Value *r) {
  IntegerType *t = cast<IntegerType>(l->getType());
  assert(l->getType() == r->getType());
  Value *lNeg = CreateIsNegative(builder, l);
  Value *rNeg = CreateIsNegative(builder, r);
  Value *add = builder.CreateAdd(l, r);
  Value *addNeg = CreateIsNegative(builder, add);
  Value *over = builder.CreateAnd(builder.CreateNot(lNeg), builder.CreateAnd(builder.CreateNot(rNeg), addNeg));
  Value *under = builder.CreateAnd(lNeg, builder.CreateAnd(rNeg, builder.CreateNot(addNeg)));

  Constant *min = ConstantInt::get(t, APInt::getSignedMinValue(t->getBitWidth()));
  Constant *max = ConstantInt::get(t, APInt::getSignedMaxValue(t->getBitWidth()));
  Value *res = builder.CreateSelect(over, max,
               builder.CreateSelect(under, min, add));
  return res;
}


static Value *CreateMinMax(IRBuilder<> &builder, bool isMax, bool isFloat, bool isSigned, Value *l, Value *r) {
  Value *cmp = isFloat ? builder.CreateFCmpOLT(l, r) :
               isSigned ? builder.CreateICmpSLT(l, r) : builder.CreateICmpULT(l, r);
  return builder.CreateSelect(cmp, isMax ? r : l, isMax ? l : r);
}

static Value *CreateAbsDiff(IRBuilder<> &builder, bool isSigned, IntegerType *tt, Value *l, Value *r) {
  Value *lmr = builder.CreateSub(builder.CreateIntCast(l, tt, isSigned),
                                 builder.CreateIntCast(r, tt, isSigned));
  Value *lmrIsNeg = CreateIsNegative(builder, lmr);
  return builder.CreateSelect(lmrIsNeg, builder.CreateNeg(lmr), lmr);
}

#define GET_ARG_OPERAND(INST, NUM) (INST)->getArgOperand(NUM)

bool LowerSSEPass::runOnBasicBlock(BasicBlock &b) { 
  bool dirty = false;
  
  for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie;) {     
    IntrinsicInst *ii = dyn_cast<IntrinsicInst>(&*i);
    // increment now since LowerIntrinsic deletion makes iterator invalid.
    ++i;  
    if(ii) {
      IRBuilder<> builder(ii->getParent(), ii);

      switch (ii->getIntrinsicID()) {
      case Intrinsic::x86_sse_storeu_ps:
      case Intrinsic::x86_sse2_storeu_dq: {
        Value *dst = GET_ARG_OPERAND(ii, 0);
        Value *src = GET_ARG_OPERAND(ii, 1);

        VectorType* vecTy = cast<VectorType>(src->getType());
        PointerType *vecPtrTy = PointerType::get(vecTy, 0);

        CastInst* pv = new BitCastInst(dst, vecPtrTy, "", ii);
        new StoreInst(src, pv, false, ii);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_storel_dq: {
        Value *dst = GET_ARG_OPERAND(ii, 0);
        Value *src = GET_ARG_OPERAND(ii, 1);

        Type *i128 = IntegerType::get(getGlobalContext(), 128);
        Type *i64 = IntegerType::get(getGlobalContext(), 64);
        Type *p0i64 = PointerType::get(i64, 0);

        Value *srci = builder.CreateBitCast(src, i128);
        Value *src64 = builder.CreateTrunc(srci, i64);

        Value *dst64 = builder.CreateBitCast(dst, p0i64);
        builder.CreateStore(src64, dst64);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_psll_dq_bs:
      case Intrinsic::x86_sse2_psrl_dq_bs: {
        Value *src = GET_ARG_OPERAND(ii, 0);
        Value *count = GET_ARG_OPERAND(ii, 1);

        Type *i128 = IntegerType::get(getGlobalContext(), 128);

        Value *srci = builder.CreateBitCast(src, i128);
        Value *count128 = builder.CreateZExt(count, i128);
        Value *countbits = builder.CreateShl(count128, 3);
        Value *resi = ii->getIntrinsicID() == Intrinsic::x86_sse2_psll_dq_bs ? builder.CreateShl(srci, countbits) : builder.CreateLShr(srci, countbits);
        Value *res = builder.CreateBitCast(resi, src->getType());

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_cvtdq2ps: {
        Value *src = GET_ARG_OPERAND(ii, 0);

        Value *res = builder.CreateSIToFP(src, ii->getType());

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_cvtps2dq: {
        Value *src = GET_ARG_OPERAND(ii, 0);

        Value *res = builder.CreateFPToSI(src, ii->getType());

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse_cvtss2si:
      case Intrinsic::x86_sse2_cvtsd2si: {
        Type *i32 = Type::getInt32Ty(getGlobalContext());

        Value *zero32 = ConstantInt::get(i32, 0);

        Value *src = GET_ARG_OPERAND(ii, 0);

        ExtractElementInst *lowElem = ExtractElementInst::Create(src, zero32, "", ii);
        FPToSIInst *conv = new FPToSIInst(lowElem, i32, "", ii);

        ii->replaceAllUsesWith(conv);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_mmx_packssdw:
      case Intrinsic::x86_sse2_packssdw_128:
      case Intrinsic::x86_mmx_packsswb:
      case Intrinsic::x86_sse2_packsswb_128:
      case Intrinsic::x86_mmx_packuswb:
      case Intrinsic::x86_sse2_packuswb_128: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        const VectorType *srcTy = cast<VectorType>(src1->getType());
        unsigned srcElCount = srcTy->getNumElements();

        assert(src2->getType() == srcTy);

        VectorType *dstTy = cast<VectorType>(ii->getType());
        IntegerType *dstElTy = cast<IntegerType>(dstTy->getElementType());
        unsigned dstElCount = dstTy->getNumElements();

        assert(srcElCount*2 == dstElCount);

        bool isSigned = (ii->getIntrinsicID() == Intrinsic::x86_mmx_packssdw
                      || ii->getIntrinsicID() == Intrinsic::x86_sse2_packssdw_128
                      || ii->getIntrinsicID() == Intrinsic::x86_mmx_packsswb
                      || ii->getIntrinsicID() == Intrinsic::x86_sse2_packsswb_128);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(dstTy);

        for (unsigned i = 0; i < srcElCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            CreateSaturatedValue(builder, isSigned, dstElTy,
                                                                 builder.CreateExtractElement(src1, ic)),
                                            ic);
        }

        for (unsigned i = 0; i < srcElCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Constant *icOfs = ConstantInt::get(i32, i+srcElCount);
          res = builder.CreateInsertElement(res,
                                            CreateSaturatedValue(builder, isSigned, dstElTy,
                                                                 builder.CreateExtractElement(src2, ic)),
                                            icOfs);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_pminu_b:
      case Intrinsic::x86_sse2_pmaxu_b:
      case Intrinsic::x86_sse2_pmins_w:
      case Intrinsic::x86_sse2_pmaxs_w:
      case Intrinsic::x86_sse_min_ps:
      case Intrinsic::x86_sse_max_ps: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);

        bool isMax = (ii->getIntrinsicID() == Intrinsic::x86_sse2_pmaxu_b
                   || ii->getIntrinsicID() == Intrinsic::x86_sse2_pmaxs_w
                   || ii->getIntrinsicID() == Intrinsic::x86_sse_max_ps);
        bool isFloat = (ii->getIntrinsicID() == Intrinsic::x86_sse_min_ps
                     || ii->getIntrinsicID() == Intrinsic::x86_sse_max_ps);
        bool isSigned = (ii->getIntrinsicID() == Intrinsic::x86_sse2_pmins_w
                      || ii->getIntrinsicID() == Intrinsic::x86_sse2_pmaxs_w);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            CreateMinMax(builder, isMax, isFloat, isSigned,
                                                         builder.CreateExtractElement(src1, ic),
                                                         builder.CreateExtractElement(src2, ic)),
                                            ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_psubus_b:
      case Intrinsic::x86_sse2_psubus_w: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            CreateSaturatedUnsignedSub(builder,
                                                         builder.CreateExtractElement(src1, ic),
                                                         builder.CreateExtractElement(src2, ic)),
                                            ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_paddus_b:
      case Intrinsic::x86_sse2_paddus_w: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            CreateSaturatedUnsignedAdd(builder,
                                                         builder.CreateExtractElement(src1, ic),
                                                         builder.CreateExtractElement(src2, ic)),
                                            ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_padds_w: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            CreateSaturatedSignedAdd(builder,
                                                         builder.CreateExtractElement(src1, ic),
                                                         builder.CreateExtractElement(src2, ic)),
                                            ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_psrai_d:
      case Intrinsic::x86_sse2_psrai_w:
      case Intrinsic::x86_sse2_psrli_d:
      case Intrinsic::x86_sse2_psrli_w:
      case Intrinsic::x86_sse2_pslli_d:
      case Intrinsic::x86_sse2_pslli_w: {
        Value *src = GET_ARG_OPERAND(ii, 0);
        Value *count = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src->getType());
        unsigned elCount = vt->getNumElements();

        assert(ii->getType() == vt);

        Instruction::BinaryOps opc;
        switch (ii->getIntrinsicID()) {
          case Intrinsic::x86_sse2_psrai_d:
          case Intrinsic::x86_sse2_psrai_w:
            opc = Instruction::AShr; break;
          case Intrinsic::x86_sse2_psrli_d:
          case Intrinsic::x86_sse2_psrli_w:
            opc = Instruction::LShr; break;
          case Intrinsic::x86_sse2_pslli_d:
          case Intrinsic::x86_sse2_pslli_w:
            opc = Instruction::Shl; break;
          default:
            assert(0 && "Unexpected intrinsic");
        }

        count = builder.CreateIntCast(count, vt->getElementType(), false);

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          res = builder.CreateInsertElement(res,
                                            builder.CreateBinOp(opc, builder.CreateExtractElement(src, ic), count),
                                            ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_pmulh_w: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        IntegerType *i16 = Type::getInt16Ty(getGlobalContext());
        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);
        assert(vt->getElementType() == i16);

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Value *v1 = builder.CreateExtractElement(src1, ic);
          Value *v2 = builder.CreateExtractElement(src2, ic);
          Value *x1 = builder.CreateSExt(v1, i32);
          Value *x2 = builder.CreateSExt(v2, i32);
          Value *mul = builder.CreateMul(x1, x2);
          Value *mulHi = builder.CreateLShr(mul, 16);
          Value *mulHiTrunc = builder.CreateTrunc(mulHi, i16);
          res = builder.CreateInsertElement(res, mulHiTrunc, ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_psad_bw: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        VectorType *rt = cast<VectorType>(ii->getType());

        IntegerType *i8 = Type::getInt8Ty(getGlobalContext());
        IntegerType *i16 = Type::getInt16Ty(getGlobalContext());
        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());
        IntegerType *i64 = Type::getInt64Ty(getGlobalContext());

        assert(src2->getType() == vt);

        assert(vt->getElementType() == i8);
        assert(vt->getNumElements() == 16);

        assert(rt->getElementType() == i64);
        assert(rt->getNumElements() == 2);

        Value *res = UndefValue::get(rt);

        Value *lo = ConstantInt::get(i16, 0);
        for (unsigned i = 0; i < 8; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Value *v1 = builder.CreateExtractElement(src1, ic);
          Value *v2 = builder.CreateExtractElement(src2, ic);
          lo = builder.CreateAdd(lo, CreateAbsDiff(builder, false, i16, v1, v2));
        }
        lo = builder.CreateZExt(lo, i64);
        res = builder.CreateInsertElement(res, lo, ConstantInt::get(i32, 0));

        Value *hi = ConstantInt::get(i16, 0);
        for (unsigned i = 8; i < 16; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Value *v1 = builder.CreateExtractElement(src1, ic);
          Value *v2 = builder.CreateExtractElement(src2, ic);
          hi = builder.CreateAdd(hi, CreateAbsDiff(builder, false, i16, v1, v2));
        }
        hi = builder.CreateZExt(hi, i64);
        res = builder.CreateInsertElement(res, hi, ConstantInt::get(i32, 1));

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_pmadd_wd: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        VectorType *rt = cast<VectorType>(ii->getType());

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        assert(src2->getType() == vt);
        assert(vt->getNumElements() == rt->getNumElements()*2);

        Value *res = UndefValue::get(rt);

        for (unsigned i = 0; i < rt->getNumElements(); i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Constant *i2p0c = ConstantInt::get(i32, i*2);
          Constant *i2p1c = ConstantInt::get(i32, i*2+1);
          Value *x0 = builder.CreateExtractElement(src1, i2p0c);
          Value *y0 = builder.CreateExtractElement(src2, i2p0c);
          Value *x1 = builder.CreateExtractElement(src1, i2p1c);
          Value *y1 = builder.CreateExtractElement(src2, i2p1c);
          x0 = builder.CreateIntCast(x0, rt->getElementType(), true);
          x1 = builder.CreateIntCast(x1, rt->getElementType(), true);
          y0 = builder.CreateIntCast(y0, rt->getElementType(), true);
          y1 = builder.CreateIntCast(y1, rt->getElementType(), true);
          Value *madd = builder.CreateAdd(builder.CreateMul(x1, y1),
                                          builder.CreateMul(x0, y0));
          res = builder.CreateInsertElement(res, madd, ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse_cmp_ps: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);
        ConstantInt *pred = cast<ConstantInt>(GET_ARG_OPERAND(ii, 2));

        static const CmpInst::Predicate pred2fcmp[] = {
          CmpInst::FCMP_OEQ, 
          CmpInst::FCMP_OLT, 
          CmpInst::FCMP_OLE, 
          CmpInst::FCMP_UNO, 
          CmpInst::FCMP_UNE, 
          CmpInst::FCMP_UGE, 
          CmpInst::FCMP_UGT, 
          CmpInst::FCMP_ORD
        };

        CmpInst::Predicate fcmpPred = pred2fcmp[pred->getZExtValue()];

        VectorType *vt = cast<VectorType>(src1->getType());

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());
        Type *f32 = Type::getFloatTy(getGlobalContext());

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);
        assert(vt->getElementType() == f32);

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < vt->getNumElements(); i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Value *v1 = builder.CreateExtractElement(src1, ic);
          Value *v2 = builder.CreateExtractElement(src2, ic);
          Value *rb = builder.CreateFCmp(fcmpPred, v1, v2);
          Value *rx = builder.CreateSExt(rb, i32);
          Value *rf = builder.CreateBitCast(rx, f32);
          res = builder.CreateInsertElement(res, rf, ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      case Intrinsic::x86_sse2_pavg_b:
      case Intrinsic::x86_sse2_pavg_w: {
        Value *src1 = GET_ARG_OPERAND(ii, 0);
        Value *src2 = GET_ARG_OPERAND(ii, 1);

        VectorType *vt = cast<VectorType>(src1->getType());
        unsigned elCount = vt->getNumElements();

        IntegerType *i32 = Type::getInt32Ty(getGlobalContext());

        assert(src2->getType() == vt);
        assert(ii->getType() == vt);

        Value *res = UndefValue::get(vt);

        for (unsigned i = 0; i < elCount; i++) {
          Constant *ic = ConstantInt::get(i32, i);
          Value *v1 = builder.CreateExtractElement(src1, ic);
          Value *v2 = builder.CreateExtractElement(src2, ic);
          Value *s = builder.CreateAdd(v1, v2);
          Value *avg = builder.CreateAShr(s, 1);
          res = builder.CreateInsertElement(res, avg, ic);
        }

        ii->replaceAllUsesWith(res);

        ii->removeFromParent();
        delete ii;
        break;
      }

      default:
        break;
      }
    }
  }

  return dirty;
}
}
