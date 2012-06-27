//===-- Executor_ExprUtil.cpp ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "Context.h"
#include "TimingSolver.h"

#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/util/GetElementPtrTypeIterator.h"

#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetData.h"

#include <glog/logging.h>

#include <iostream>
#include <cassert>
#include <sstream>

using namespace klee;
using namespace llvm;

namespace klee {

/* Concretize the given expression, and return a possible constant value.
   'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr>
Executor::toConstant(ExecutionState &state,
                     ref<Expr> e,
                     const char *reason) {
  e = state.constraints().simplifyExpr(e);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE;

  ref<ConstantExpr> value;
  bool success = solver->getValue(data::EXPRESSION_CONCRETIZATION, state, e, value);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;

  std::ostringstream os;
  os << "silently concretizing (reason: " << reason << ") expression " << e
     << " to value " << value
     << " (" << (*(state.pc())).info->file << ":" << (*(state.pc())).info->line << ")";


  LOG(WARNING) << reason << os.str().c_str();

  addConstraint(state, EqExpr::create(e, value));

  return value;
}

Expr::Width Executor::getWidthForLLVMType(llvm::Type *type) const {
  return kmodule->targetData->getTypeSizeInBits(type);
}

ref<Expr> Executor::toUnique(const ExecutionState &state,
                             ref<Expr> &e) {
  ref<Expr> result = e;

  if (!isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool isTrue = false;

    solver->setTimeout(stpTimeout);
    if (solver->getValue(data::CHECK_UNIQUENESS, state, e, value) &&
        solver->mustBeTrue(data::CHECK_UNIQUENESS, state, EqExpr::create(e, value), isTrue) &&
        isTrue)
      result = value;
    solver->setTimeout(0);
  }

  return result;
}

ref<ConstantExpr> Executor::evalConstantExpr(const llvm::ConstantExpr *ce) {
  llvm::Type *type = ce->getType();

  ref<ConstantExpr> op1(0), op2(0), op3(0);
  int numOperands = ce->getNumOperands();

  if (numOperands > 0) op1 = evalConstant(ce->getOperand(0));
  if (numOperands > 1) op2 = evalConstant(ce->getOperand(1));
  if (numOperands > 2) op3 = evalConstant(ce->getOperand(2));

  switch (ce->getOpcode()) {
  default :
    ce->dump();
    std::cerr << "error: unknown ConstantExpr type\n"
              << "opcode: " << ce->getOpcode() << "\n";
    abort();

  case Instruction::Trunc:
    return op1->Extract(0, getWidthForLLVMType(type));
  case Instruction::ZExt:  return op1->ZExt(getWidthForLLVMType(type));
  case Instruction::SExt:  return op1->SExt(getWidthForLLVMType(type));
  case Instruction::Add:   return op1->Add(op2);
  case Instruction::Sub:   return op1->Sub(op2);
  case Instruction::Mul:   return op1->Mul(op2);
  case Instruction::SDiv:  return op1->SDiv(op2);
  case Instruction::UDiv:  return op1->UDiv(op2);
  case Instruction::SRem:  return op1->SRem(op2);
  case Instruction::URem:  return op1->URem(op2);
  case Instruction::And:   return op1->And(op2);
  case Instruction::Or:    return op1->Or(op2);
  case Instruction::Xor:   return op1->Xor(op2);
  case Instruction::Shl:   return op1->Shl(op2);
  case Instruction::LShr:  return op1->LShr(op2);
  case Instruction::AShr:  return op1->AShr(op2);
  case Instruction::BitCast:  return op1;

  case Instruction::IntToPtr:
    return op1->ZExt(getWidthForLLVMType(type));

  case Instruction::PtrToInt:
    return op1->ZExt(getWidthForLLVMType(type));

  case Instruction::GetElementPtr: {
    ref<ConstantExpr> base = op1->ZExt(Context::get().getPointerWidth());

    for (gep_type_iterator ii = gep_type_begin(ce), ie = gep_type_end(ce);
         ii != ie; ++ii) {
      ref<ConstantExpr> addend =
        ConstantExpr::alloc(0, Context::get().getPointerWidth());

      if (StructType *st = dyn_cast<StructType>(*ii)) {
        const StructLayout *sl = kmodule->targetData->getStructLayout(st);
        const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());

        addend = ConstantExpr::alloc(sl->getElementOffset((unsigned)
                                                          ci->getZExtValue()),
                                     Context::get().getPointerWidth());
      } else {
        const SequentialType *set = cast<SequentialType>(*ii);
        ref<ConstantExpr> index =
          evalConstant(cast<Constant>(ii.getOperand()));
        unsigned elementSize =
          kmodule->targetData->getTypeStoreSize(set->getElementType());

        index = index->ZExt(Context::get().getPointerWidth());
        addend = index->Mul(ConstantExpr::alloc(elementSize,
                                                Context::get().getPointerWidth()));
      }

      base = base->Add(addend);
    }

    return base;
  }

  case Instruction::ICmp: {
    switch(ce->getPredicate()) {
    default: assert(0 && "unhandled ICmp predicate");
    case ICmpInst::ICMP_EQ:  return op1->Eq(op2);
    case ICmpInst::ICMP_NE:  return op1->Ne(op2);
    case ICmpInst::ICMP_UGT: return op1->Ugt(op2);
    case ICmpInst::ICMP_UGE: return op1->Uge(op2);
    case ICmpInst::ICMP_ULT: return op1->Ult(op2);
    case ICmpInst::ICMP_ULE: return op1->Ule(op2);
    case ICmpInst::ICMP_SGT: return op1->Sgt(op2);
    case ICmpInst::ICMP_SGE: return op1->Sge(op2);
    case ICmpInst::ICMP_SLT: return op1->Slt(op2);
    case ICmpInst::ICMP_SLE: return op1->Sle(op2);
    }
  }

  case Instruction::Select:
    return op1->isTrue() ? op2 : op3;

  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FCmp:
    assert(0 && "floating point ConstantExprs unsupported");
  }
}

ref<klee::ConstantExpr> Executor::evalConstant(const Constant *c) {
  if (getWidthForLLVMType(c->getType()) == 0) {
    std::string conststr;
    llvm::raw_string_ostream rso(conststr);
    rso << *c;
    rso.flush();
    LOG(INFO) << "Zero-width constant: '" << c->getName().str() << "' " << conststr;

    for (Constant::const_use_iterator it = c->use_begin(), ie = c->use_end();
        it != ie; it++) {
      const Value *value = *it;
      std::string instr;
      llvm::raw_string_ostream rso(instr);
      rso << *value;
      rso.flush();
      LOG(INFO) << "Constant use: " << instr;
    }
  }

  if (const llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
    return evalConstantExpr(ce);
  } else {
    if (const ConstantInt *ci = dyn_cast<ConstantInt>(c)) {
      return ConstantExpr::alloc(ci->getValue());
    } else if (const ConstantFP *cf = dyn_cast<ConstantFP>(c)) {
      return ConstantExpr::alloc(cf->getValueAPF().bitcastToAPInt());
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
      return globalAddresses.find(gv)->second;
    } else if (isa<ConstantPointerNull>(c)) {
      return Expr::createPointer(0);
    } else if (isa<UndefValue>(c) || isa<ConstantAggregateZero>(c)) {
      Expr::Width w = getWidthForLLVMType(c->getType());
      return ConstantExpr::create(0, (w > 0) ? w : 1);
    } else if (const ConstantDataSequential *cds =
        dyn_cast<ConstantDataSequential>(c)) {
      std::vector<ref<Expr> > kids;
      for (unsigned i = 0, e = cds->getNumElements(); i != e; ++i) {
        ref<Expr> kid = evalConstant(cds->getElementAsConstant(i));
        kids.push_back(kid);
      }
      ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
      return cast<ConstantExpr>(res);
    } else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
      if(cs->getNumOperands() == 0)
        return Expr::createPointer(0);
      ref<klee::ConstantExpr> result = evalConstant(cs->getOperand(0));
      for (unsigned k=1, e=cs->getNumOperands(); k != e; ++k){
        ref<klee::ConstantExpr> next = evalConstant(cs->getOperand(k));
        result = next->Concat(result);
      }
      return result;
    }else if (const ConstantVector *cv = dyn_cast<ConstantVector>(c)) {
      if(cv->getNumOperands() == 0)
        return Expr::createPointer(0);
      ref<klee::ConstantExpr> result = evalConstant(cv->getOperand(0));
      for (unsigned k=1, e=cv->getNumOperands(); k != e; ++k){
        ref<klee::ConstantExpr> next = evalConstant(cv->getOperand(k));
        result = next->Concat(result);
      }
      return result;
    } else {
      // Constant{Array,Struct,Vector}
      assert(0 && "invalid argument to evalConstant()");
    }
  }
}

const Cell& Executor::eval(KInstruction *ki, unsigned index,
                           ExecutionState &state) const {
  LOG_IF(INFO, index >= ki->inst->getNumOperands())
      << "Line: " << ki->info->assemblyLine
      << " Index: " << index
      << " Num. op.: " << ki->inst->getNumOperands();

  assert(index < ki->inst->getNumOperands());
  int vnumber = ki->operands[index];

  assert(vnumber != -1 &&
         "Invalid operand to eval(), not a value or constant!");

  // Determine if this is a constant or not.
  if (vnumber < 0) {
    unsigned index = -vnumber - 2;
    return kmodule->constantTable[index];
  } else {
    unsigned index = vnumber;
    StackFrame &sf = state.stack().back();
    return sf.locals[index];
  }
}

}
