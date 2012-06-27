/*
 * LoggingSolvers.h
 *
 *  Created on: Mar 29, 2012
 *      Author: bucur
 */

#ifndef LOGGINGSOLVERS_H_
#define LOGGINGSOLVERS_H_

#include "klee/Solver.h"

#include "klee/SolverImpl.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/util/ExprHashMap.h"
#include "klee/util/ExprPPrinter.h"

#include <fstream>
#include <map>

using namespace llvm;
using namespace klee::util;

namespace klee {

class LoggingSolver: public SolverImpl {
public:
  LoggingSolver(std::string path)
  : os(path.c_str(), std::ios::trunc),
    printer(ExprPPrinter::create(os)),
    queryCount(0) {

  }

  ~LoggingSolver() {
    delete printer;
  }

protected:
  void startQuery(const Query& query,
                  const char *typeName,
                  const std::string &parentID = std::string(),
                  const std::string &queryID = std::string(),
                  const ref<Expr> *evalExprsBegin = 0,
                  const ref<Expr> *evalExprsEnd = 0,
                  const Array * const* evalArraysBegin = 0,
                  const Array * const* evalArraysEnd = 0) {
    Statistic *S = theStatisticManager->getStatisticByName("Instructions");
    uint64_t instructions = S ? S->getValue() : 0;
    os << "# Query " << queryCount++ << " -- "
       << "Type: " << typeName << ", "
       << "Instructions: " << instructions << "\n";
    printer->printQuery(os, query.constraints, query.expr,
                        parentID, queryID,
                        evalExprsBegin, evalExprsEnd,
                        evalArraysBegin, evalArraysEnd);
    os.flush();

    startTime = getWallTime();
  }

  void finishQuery(bool success) {
    double delta = getWallTime() - startTime;
    os << "#   " << (success ? "OK" : "FAIL") << " -- "
       << "Elapsed: " << delta << "\n";
  }

  std::ofstream os;

private:
  ExprPPrinter *printer;
  double startTime;
  unsigned queryCount;
};

class PCLoggingSolver : public LoggingSolver {
  Solver *solver;

public:
  PCLoggingSolver(Solver *_solver, std::string path)
  : LoggingSolver(path), solver(_solver) { }

  ~PCLoggingSolver() {
    delete solver;
  }

  bool computeTruth(const Query& query, bool &isValid) {
    startQuery(query, "Truth");
    bool success = solver->impl->computeTruth(query, isValid);
    finishQuery(success);
    if (success)
      os << "#   Is Valid: " << (isValid ? "true" : "false") << "\n";
    os << "\n";
    return success;
  }

  bool computeValidity(const Query& query, Solver::Validity &result) {
    startQuery(query, "Validity");
    bool success = solver->impl->computeValidity(query, result);
    finishQuery(success);
    if (success)
      os << "#   Validity: " << result << "\n";
    os << "\n";
    return success;
  }

  bool computeValue(const Query& query, ref<Expr> &result) {
    startQuery(query.withFalse(), "Value",
               std::string(), std::string(),
               &query.expr, &query.expr + 1);
    bool success = solver->impl->computeValue(query, result);
    finishQuery(success);
    if (success)
      os << "#   Result: " << result << "\n";
    os << "\n";
    return success;
  }

  bool computeInitialValues(const Query& query,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution) {
    if (objects.empty()) {
      startQuery(query, "InitialValues",
                 0, 0);
    } else {
      startQuery(query, "InitialValues",
                 std::string(), std::string(),
                 0, 0,
                 &objects[0], &objects[0] + objects.size());
    }
    bool success = solver->impl->computeInitialValues(query, objects,
                                                      values, hasSolution);
    finishQuery(success);
    if (success) {
      os << "#   Solvable: " << (hasSolution ? "true" : "false") << "\n";
      if (hasSolution) {
        std::vector< std::vector<unsigned char> >::iterator
          values_it = values.begin();
        for (std::vector<const Array*>::const_iterator i = objects.begin(),
               e = objects.end(); i != e; ++i, ++values_it) {
          const Array *array = *i;
          std::vector<unsigned char> &data = *values_it;
          os << "#     " << array->name << " = [";
          for (unsigned j = 0; j < array->size; j++) {
            os << (int) data[j];
            if (j+1 < array->size)
              os << ",";
          }
          os << "]\n";
        }
      }
    }
    os << "\n";
    return success;
  }
};

}


#endif /* LOGGINGSOLVERS_H_ */
