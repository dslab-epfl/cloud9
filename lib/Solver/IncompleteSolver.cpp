//===-- IncompleteSolver.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/IncompleteSolver.h"

#include "klee/Constraints.h"

using namespace klee;
using namespace llvm;

/***/

Solver::PartialValidity
IncompleteSolver::negatePartialValidity(Solver::PartialValidity pv) {
  switch(pv) {
  default: assert(0 && "invalid partial validity");  
  case Solver::MustBeTrue:  return Solver::MustBeFalse;
  case Solver::MustBeFalse: return Solver::MustBeTrue;
  case Solver::MayBeTrue:   return Solver::MayBeFalse;
  case Solver::MayBeFalse:  return Solver::MayBeTrue;
  case Solver::TrueOrFalse: return Solver::TrueOrFalse;
  }
}

Solver::PartialValidity
IncompleteSolver::computeValidity(const Query& query) {
  Solver::PartialValidity trueResult = computeTruth(query);

  if (trueResult == Solver::MustBeTrue) {
    return Solver::MustBeTrue;
  } else {
    Solver::PartialValidity falseResult = computeTruth(query.negateExpr());

    if (falseResult == Solver::MustBeTrue) {
      return Solver::MustBeFalse;
    } else {
      bool trueCorrect = trueResult != Solver::None,
        falseCorrect = falseResult != Solver::None;
      
      if (trueCorrect && falseCorrect) {
        return Solver::TrueOrFalse;
      } else if (trueCorrect) { // ==> trueResult == MayBeFalse
        return Solver::MayBeFalse;
      } else if (falseCorrect) { // ==> falseResult == MayBeFalse
        return Solver::MayBeTrue;
      } else {
        return Solver::None;
      }
    }
  }
}

/***/

StagedSolverImpl::StagedSolverImpl(IncompleteSolver *_primary, 
                                   Solver *_secondary) 
  : primary(_primary),
    secondary(_secondary) {
}

StagedSolverImpl::~StagedSolverImpl() {
  delete primary;
  delete secondary;
}

bool StagedSolverImpl::computeTruth(const Query& query, bool &isValid) {
  Solver::PartialValidity trueResult = primary->computeTruth(query);
  
  if (trueResult != Solver::None) {
    isValid = (trueResult == Solver::MustBeTrue);
    return true;
  } 

  return secondary->impl->computeTruth(query, isValid);
}

bool StagedSolverImpl::computeValidity(const Query& query,
                                       Solver::Validity &result) {
  bool tmp;

  switch(primary->computeValidity(query)) {
  case Solver::MustBeTrue:
    result = Solver::True;
    break;
  case Solver::MustBeFalse:
    result = Solver::False;
    break;
  case Solver::TrueOrFalse:
    result = Solver::Unknown;
    break;
  case Solver::MayBeTrue:
    if (!secondary->impl->computeTruth(query, tmp))
      return false;
    result = tmp ? Solver::True : Solver::Unknown;
    break;
  case Solver::MayBeFalse:
    if (!secondary->impl->computeTruth(query.negateExpr(), tmp))
      return false;
    result = tmp ? Solver::False : Solver::Unknown;
    break;
  default:
    if (!secondary->impl->computeValidity(query, result))
      return false;
    break;
  }

  return true;
}

bool StagedSolverImpl::computeValue(const Query& query,
                                    ref<Expr> &result) {
  if (primary->computeValue(query, result))
    return true;

  return secondary->impl->computeValue(query, result);
}

bool 
StagedSolverImpl::computeInitialValues(const Query& query,
                                       const std::vector<const Array*> 
                                         &objects,
                                       std::vector< std::vector<unsigned char> >
                                         &values,
                                       bool &hasSolution) {
  if (primary->computeInitialValues(query, objects, values, hasSolution))
    return true;
  
  return secondary->impl->computeInitialValues(query, objects, values,
                                               hasSolution);
}
