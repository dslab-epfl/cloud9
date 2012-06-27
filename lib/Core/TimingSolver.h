//===-- TimingSolver.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TIMINGSOLVER_H
#define KLEE_TIMINGSOLVER_H

#include "klee/Expr.h"
#include "klee/Solver.h"

#include "klee/data/ConstraintSolving.pb.h"

#include <boost/shared_ptr.hpp>
using boost::shared_ptr;

#include <vector>

namespace klee {
  class ExecutionState;
  class Solver;
  class STPSolver;
  class StatsTracker;

  /// TimingSolver - A simple class which wraps a solver and handles
  /// tracking the statistics that we care about.
  class TimingSolver {
  public:
    Solver *solver;
    STPSolver *stpSolver;
    StatsTracker *statsTracker;

    bool simplifyExprs;

  public:
    /// TimingSolver - Construct a new timing solver.
    ///
    /// \param _simplifyExprs - Whether expressions should be
    /// simplified (via the constraint manager interface) prior to
    /// querying.
    TimingSolver(Solver *_solver, STPSolver *_stpSolver,
                 StatsTracker *_statsTracker,
                 bool _simplifyExprs = true)
        : solver(_solver), stpSolver(_stpSolver),
        statsTracker(_statsTracker), simplifyExprs(_simplifyExprs) {}

    ~TimingSolver() {
      delete solver;
    }

    void setTimeout(double t) {
      if (stpSolver)
        stpSolver->setTimeout(t);
    }

    bool evaluate(data::QueryReason reason, const ExecutionState&,
        ref<Expr>, Solver::Validity &result);

    bool mustBeTrue(data::QueryReason reason, const ExecutionState&, ref<Expr>, bool &result);

    bool mustBeFalse(data::QueryReason reason, const ExecutionState&, ref<Expr>, bool &result);

    bool mayBeTrue(data::QueryReason reason, const ExecutionState&, ref<Expr>, bool &result);

    bool mayBeFalse(data::QueryReason reason, const ExecutionState&, ref<Expr>, bool &result);

    bool getValue(data::QueryReason reason, const ExecutionState &, ref<Expr> expr,
                  ref<ConstantExpr> &result);

    bool getInitialValues(data::QueryReason reason, const ExecutionState&,
                          const std::vector<const Array*> &objects,
                          std::vector< std::vector<unsigned char> > &result);

    std::pair< ref<Expr>, ref<Expr> >
    getRange(data::QueryReason reason, const ExecutionState&, ref<Expr> query);
  };

}

#endif
