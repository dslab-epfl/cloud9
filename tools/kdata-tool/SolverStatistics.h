/*
 * SolverStatistics.h
 *
 *  Created on: Apr 24, 2012
 *      Author: bucur
 */

#ifndef SOLVERSTATISTICS_H_
#define SOLVERSTATISTICS_H_

#include "klee/data/ConstraintSolving.pb.h"

#include <ostream>

namespace klee {

class SolverStatistics {
public:
  SolverStatistics()
    : total_queries_(0),
      total_solving_time_(0),
      max_solving_time_(0),
      min_solving_time_(0),
      average_solving_time_(0),
      median_solving_time_(0) {

  }
  virtual ~SolverStatistics() { }

  void ComputeStatistics(const data::SolverQuerySet &query_set);
  void Print(std::ostream &os);

  int total_queries() const { return total_queries_; }
  uint64_t total_solving_time() const { return total_solving_time_; }
  uint64_t max_solving_time() const { return max_solving_time_; }
  uint64_t min_solving_time() const { return min_solving_time_; }
  uint64_t average_solving_time() const { return average_solving_time_; }
  uint64_t median_solving_time() const { return median_solving_time_; }
private:
  int total_queries_;
  uint64_t total_solving_time_;
  uint64_t max_solving_time_;
  uint64_t min_solving_time_;
  uint64_t average_solving_time_;
  uint64_t median_solving_time_;
};

} /* namespace klee */
#endif /* SOLVERSTATISTICS_H_ */
