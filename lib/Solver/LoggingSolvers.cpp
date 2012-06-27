//===-- PCLoggingSolver.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/LoggingSolvers.h"

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/Internal/Support/QueryLog.h"

#include "llvm/Support/CommandLine.h"

#include <iomanip>
#include <sstream>

#include <glog/logging.h>

///

namespace klee {

Solver *createPCLoggingSolver(Solver *_solver, std::string path) {
  return new Solver(new PCLoggingSolver(_solver, path));
}

} /* namespace klee */

///
