/*
 * SolverStatistics.cpp
 *
 *  Created on: Apr 24, 2012
 *      Author: bucur
 */

#include "SolverStatistics.h"

#include "llvm/Support/CommandLine.h"

#include <set>

using namespace llvm;

namespace {
cl::opt<bool> ComputeMedian("compute-median",
                            cl::desc("Compute median information"),
                            cl::init(true));
}

namespace klee {

void SolverStatistics::ComputeStatistics(const data::SolverQuerySet &query_set) {
  total_queries_ = 0;
  total_solving_time_ = 0;
  max_solving_time_ = 0;
  min_solving_time_ = (uint64_t)-1;
  std::multiset<uint64_t> solving_time_values;

  for (int i = 0; i < query_set.solver_query_size(); i++) {
    const data::SolverQuery &query = query_set.solver_query(i);
    total_queries_++;
    total_solving_time_ += query.solving_time();

    if (ComputeMedian) {
      solving_time_values.insert(query.solving_time());
    }

    if (query.solving_time() > max_solving_time_)
      max_solving_time_ = query.solving_time();
    if (query.solving_time() < min_solving_time_)
      min_solving_time_ = query.solving_time();
  }

  if (total_queries_ > 0) {
    average_solving_time_ = total_solving_time_ / total_queries_;
  } else {
    average_solving_time_ = 0;
  }

  if (ComputeMedian) {
    if (total_queries_ > 0) {
      int count = 0;
      median_solving_time_ = 0;
      for (std::multiset<uint64_t>::iterator it = solving_time_values.begin(),
          ie = solving_time_values.end(); it != ie; ++it) {
        if (count == (total_queries_ - 1) / 2) {
          median_solving_time_ = *it;
          if (total_queries_ % 2 == 0) {
            median_solving_time_ = (median_solving_time_ + *(++it))/2;
          }
          break;
        }
        count++;
      }
    } else {
      median_solving_time_ = 0;
    }
  }
}

void SolverStatistics::Print(std::ostream &os) {
  os << total_queries_ << " "
      << total_solving_time_ << " "
      << max_solving_time_ << " "
      << min_solving_time_ << " "
      << average_solving_time_ << " "
      << median_solving_time_;
}

} /* namespace klee */
