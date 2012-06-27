/*
 * Aggregators.h
 *
 *  Created on: Apr 25, 2012
 *      Author: bucur
 */

#ifndef AGGREGATORS_H_
#define AGGREGATORS_H_

#include "BaseAggregator.h"

#include "klee/data/ConstraintSolving.pb.h"

#include <iostream>
#include <map>
#include <string>

#include <stdint.h>

namespace klee {

////////////////////////////////////////////////////////////////////////////////

class ExecutionEffortAggregator: public BaseAggregator {
public:
  typedef std::map<std::string, uint64_t> PathIDGrouping;
  typedef std::map<uint64_t, PathIDGrouping*> PathDepthGrouping;

  ExecutionEffortAggregator(PathLengthMetric path_metric,
                            uint64_t path_bucket_size)
    : BaseAggregator(path_metric, path_bucket_size) {

  }

  ~ExecutionEffortAggregator() {
    for (PathDepthGrouping::iterator it = query_grouping_.begin(),
           ie = query_grouping_.end(); it != ie; ++it) {
      delete it->second;
    }
  }

  virtual void ProcessSolverQuery(const data::SolverQuery &query);
  virtual void PrintStatistics(std::ostream &os);

private:
  PathDepthGrouping query_grouping_;
};

////////////////////////////////////////////////////////////////////////////////

enum QueryHLReason {
  HL_BEGIN = 0,

  HL_BRANCH_FEASIBILITY = 0,
  HL_MEMORY_CHECK = 1,
  HL_OTHERS = 2,

  HL_END = 3
};

class QueryReasonAggregator: public BaseAggregator {
public:
  typedef std::map<QueryHLReason, data::SolverQuerySet> QueryReasonGrouping;
  typedef std::map<uint64_t, QueryReasonGrouping*> PathDepthGrouping;

  QueryReasonAggregator(PathLengthMetric path_metric, uint64_t path_bucket_size)
    : BaseAggregator(path_metric, path_bucket_size) {

  }

  virtual ~QueryReasonAggregator() {
    for (PathDepthGrouping::iterator it = query_grouping_.begin(),
        ie = query_grouping_.end(); it != ie; ++it) {
      delete it->second;
    }
  }

  virtual void ProcessSolverQuery(const data::SolverQuery &query);
  virtual void PrintStatistics(std::ostream &os);

private:
  PathDepthGrouping query_grouping_;
};

class QueryTimeAggregator {
public:
  QueryTimeAggregator() { }
  ~QueryTimeAggregator() { }

  void ProcessSolverQuery(const data::SolverQuery &query);
  void PrintStatistics(std::ostream &os);

private:
  data::SolverQuerySet query_set_;
};

} /* namespace klee */
#endif /* AGGREGATORS_H_ */
