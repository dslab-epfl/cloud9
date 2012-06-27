/*
 * BaseAggregator.h
 *
 *  Created on: Apr 25, 2012
 *      Author: bucur
 */

#ifndef BASEAGGREGATOR_H_
#define BASEAGGREGATOR_H_

#include "klee/data/ConstraintSolving.pb.h"
#include <stdint.h>

#include <map>
#include <ostream>
#include <set>

namespace klee {

/// Path-insensitive analysis
class BaseAggregator {
public:
  enum PathLengthMetric {
    INSTRUCTIONS,
    BRANCHES,
    FORKS,
    QUERIES,
    TIME
  };

  BaseAggregator(PathLengthMetric path_metric, uint64_t path_bucket_size)
    : path_metric_(path_metric),
      path_bucket_size_(path_bucket_size) {

  }
  virtual ~BaseAggregator() { }

  virtual void ProcessSolverQuery(const data::SolverQuery &query) = 0;
  virtual void PrintStatistics(std::ostream &os) = 0;

  uint64_t GetQueryDepth(const data::SolverQuery &query) const;

  PathLengthMetric path_metric() const { return path_metric_; }
  uint64_t path_bucket_size() const { return path_bucket_size_; }

private:
  PathLengthMetric path_metric_;
  uint64_t path_bucket_size_;
};


struct SolverQueryComparator {
  bool operator()(const data::SolverQuery *q1, const data::SolverQuery *q2) {
    return q1->execution_state().instructions() < q2->execution_state().instructions();
  }
};

typedef std::set<data::SolverQuery*, SolverQueryComparator> OrderedSolverQuerySet;
typedef std::vector<data::SolverQuery*> SolverQueryVector;

class ExecutionTrace {
public:
  ExecutionTrace(const std::string &id, const std::string &parent_id)
   : id_(id), parent_id_(parent_id) { }
  ~ExecutionTrace() { }

  const OrderedSolverQuerySet &query_set() const { return ordered_query_set_; }

  const std::string &id() { return id_; }
  const std::string &parent_id() { return parent_id_; }

private:
  friend class PathSensitiveAnalyzer;

  std::string id_;
  std::string parent_id_;

  data::SolverQuerySet query_set_;
  OrderedSolverQuerySet ordered_query_set_;
};

typedef std::map<std::string, ExecutionTrace*> ExecutionTraces;

/// Path-sensitive analysis
class PathSensitiveAnalyzer: public BaseAggregator {
public:
  PathSensitiveAnalyzer(PathLengthMetric path_metric, uint64_t path_bucket_size)
    : BaseAggregator(path_metric, path_bucket_size) {

  }
  virtual ~PathSensitiveAnalyzer() { }

  virtual void ProcessSolverQuery(const data::SolverQuery &query);
  virtual void PrintStatistics(std::ostream &os);

  uint64_t GetPathLength(const ExecutionTrace *trace) const;
  uint64_t GetTraceLength(const ExecutionTrace *trace) const;
  void SampleTrace(const ExecutionTrace *trace, unsigned int sample_rate,
                   SolverQueryVector &sample);

  const ExecutionTraces &execution_traces() const { return execution_traces_; }

private:
  ExecutionTraces execution_traces_;
};

class PathSensitiveDistribution: public PathSensitiveAnalyzer {
public:
  PathSensitiveDistribution(PathLengthMetric path_metric, uint64_t path_bucket_size)
    : PathSensitiveAnalyzer(path_metric, path_bucket_size) {

  }

  virtual ~PathSensitiveDistribution() { }

  virtual void PrintStatistics(std::ostream &os);
};



} /* namespace klee */
#endif /* BASEAGGREGATOR_H_ */
