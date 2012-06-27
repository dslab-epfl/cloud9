//===-- TimingSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TimingSolver.h"

#include "klee/CoreStats.h"
#include "klee/ExecutionState.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/util/ExprPPrinter.h"

#include "StatsTracker.h"

#include "llvm/Support/Process.h"

using namespace klee;
using namespace llvm;

/***/

bool TimingSolver::evaluate(data::QueryReason reason,
    const ExecutionState& state, ref<Expr> expr, Solver::Validity &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? Solver::True : Solver::False;
    return true;
  }

  sys::TimeValue now(0,0),user(0,0),delta(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);

  if (simplifyExprs)
    expr = state.constraints().simplifyExpr(expr);

  bool success = solver->evaluate(Query(state.constraints(), expr), result);

  sys::Process::GetTimeUsage(delta,user,sys);
  delta -= now;
  stats::solverTime += delta.usec();
  state.queryCost += delta.usec()/1000000.;
  state.totalTime += delta.usec();
  ++state.totalQueries;

  statsTracker->recordSolverQuery(now.toPosixTime(), delta.usec(), reason,
      data::EVALUATE, false, state);

  return success;
}

bool TimingSolver::mustBeTrue(data::QueryReason reason, const ExecutionState& state,
    ref<Expr> expr, bool &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  sys::TimeValue now(0,0),user(0,0),delta(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);

  if (simplifyExprs)
    expr = state.constraints().simplifyExpr(expr);

  bool success = solver->mustBeTrue(Query(state.constraints(), expr), result);

  sys::Process::GetTimeUsage(delta,user,sys);
  delta -= now;
  stats::solverTime += delta.usec();
  state.queryCost += delta.usec()/1000000.;
  state.totalTime += delta.usec();
  ++state.totalQueries;

  statsTracker->recordSolverQuery(now.toPosixTime(), delta.usec(), reason,
      data::MUST_BE_TRUE, false, state);

  return success;
}

bool TimingSolver::mustBeFalse(data::QueryReason reason, const ExecutionState& state, ref<Expr> expr,
                               bool &result) {
  return mustBeTrue(reason, state, Expr::createIsZero(expr), result);
}

bool TimingSolver::mayBeTrue(data::QueryReason reason, const ExecutionState& state, ref<Expr> expr,
                             bool &result) {
  bool res;
  if (!mustBeFalse(reason, state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::mayBeFalse(data::QueryReason reason, const ExecutionState& state, ref<Expr> expr,
                              bool &result) {
  bool res;
  if (!mustBeTrue(reason, state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::getValue(data::QueryReason reason, const ExecutionState& state, ref<Expr> expr,
                            ref<ConstantExpr> &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE;
    return true;
  }
  
  sys::TimeValue now(0,0),user(0,0),delta(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);

  if (simplifyExprs)
    expr = state.constraints().simplifyExpr(expr);

  bool success = false;

	success = solver->getValue(Query(state.constraints(), expr), result);

  sys::Process::GetTimeUsage(delta,user,sys);
  delta -= now;
  stats::solverTime += delta.usec();
  state.queryCost += delta.usec()/1000000.;
  state.totalTime += delta.usec();
  ++state.totalQueries;

  statsTracker->recordSolverQuery(now.toPosixTime(), delta.usec(), reason,
      data::GET_VALUE, false, state);

  return success;
}

bool 
TimingSolver::getInitialValues(data::QueryReason reason,
                               const ExecutionState& state,
                               const std::vector<const Array*>
                                 &objects,
                               std::vector< std::vector<unsigned char> >
                                 &result) {
  if (objects.empty())
    return true;

  sys::TimeValue now(0,0),user(0,0),delta(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);

  bool success = solver->getInitialValues(Query(state.constraints(),
                                             ConstantExpr::alloc(0, Expr::Bool)),
                                       objects, result);

  sys::Process::GetTimeUsage(delta,user,sys);
  delta -= now;
  stats::solverTime += delta.usec();
  state.queryCost += delta.usec()/1000000.;
  state.totalTime += delta.usec();
  ++state.totalQueries;
  
  statsTracker->recordSolverQuery(now.toPosixTime(), delta.usec(), reason,
      data::GET_INITIAL_VALUES, false, state);

  return success;
}

std::pair< ref<Expr>, ref<Expr> >
TimingSolver::getRange(data::QueryReason reason, const ExecutionState& state, ref<Expr> expr) {
  sys::TimeValue now(0,0),user(0,0),delta(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);

  std::pair< ref<Expr>, ref<Expr> > result = solver->getRange(Query(state.constraints(), expr));

  sys::Process::GetTimeUsage(delta,user,sys);
  delta -= now;

  statsTracker->recordSolverQuery(now.toPosixTime(), delta.usec(), reason,
      data::GET_RANGE, false, state);

  return result;
}
