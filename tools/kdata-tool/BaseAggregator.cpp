/*
 * BaseAggregator.cpp
 *
 *  Created on: Apr 25, 2012
 *      Author: bucur
 */

#include "BaseAggregator.h"

#include <assert.h>

#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace {
cl::opt<unsigned int> PerPathSamples("per-path-samples",
    cl::desc("Number of solver samples to acquire for each path"),
    cl::init(10));
}

namespace klee {

void PathSensitiveAnalyzer::ProcessSolverQuery(const data::SolverQuery &query) {
  assert(query.has_execution_state() && "Cannot parse query");

  // Identify the path
  if (!execution_traces_.count(query.execution_state().id()))
    execution_traces_[query.execution_state().id()] = new ExecutionTrace(
        query.execution_state().id(), query.execution_state().parent_id());

  ExecutionTrace *execution_trace = execution_traces_[query.execution_state().id()];

  data::SolverQuery *query_copy = execution_trace->query_set_.add_solver_query();
  query_copy->CopyFrom(query);
  execution_trace->ordered_query_set_.insert(query_copy);
}

void PathSensitiveAnalyzer::PrintStatistics(std::ostream &os) {
  os << "# Total numer of paths:" << std::endl;
  os << execution_traces_.size() << std::endl;

  std::map<uint64_t, uint64_t> distribution;

  uint64_t average_path_length = 0;
  uint64_t average_trace_length = 0;
  for (ExecutionTraces::iterator it = execution_traces_.begin(),
      ie = execution_traces_.end(); it != ie; ++it) {
    uint64_t path_length = GetPathLength(it->second);
    average_path_length += path_length;
    average_trace_length += GetTraceLength(it->second);
    distribution[(path_length / path_bucket_size()) * path_bucket_size()] += 1;
  }
  average_path_length /= execution_traces_.size();
  average_trace_length /= execution_traces_.size();

  os << "# Average path length:" << std::endl;
  os << average_path_length << std::endl;
  os << "# Average trace length:" << std::endl;
  os << average_trace_length << std::endl;
  os << std::endl << std::endl;

  os << "# Path length distribution:" << std::endl;
  for (std::map<uint64_t, uint64_t>::iterator it = distribution.begin(),
      ie = distribution.end(); it != ie; ++it) {
    os << it->first << " " << it->second << std::endl;
  }
}

uint64_t PathSensitiveAnalyzer::GetPathLength(const ExecutionTrace *trace) const {
  OrderedSolverQuerySet::iterator it = trace->query_set().end();
  --it;

  return GetQueryDepth(*(*it));
}

uint64_t PathSensitiveAnalyzer::GetTraceLength(const ExecutionTrace *trace) const {
  OrderedSolverQuerySet::iterator it = trace->query_set().end();
  --it;

  uint64_t max_depth = GetQueryDepth(*(*it));

  it = trace->query_set().begin();

  uint64_t min_depth = GetQueryDepth(*(*it));

  return max_depth - min_depth;
}

void PathSensitiveAnalyzer::SampleTrace(const ExecutionTrace *trace,
                                        unsigned int sample_rate,
                                        SolverQueryVector &sample) {
  unsigned int size = trace->ordered_query_set_.size();

  unsigned int index = 0;
  unsigned int sample_index = 0;
  for (OrderedSolverQuerySet::const_iterator it = trace->query_set().begin(),
      ie = trace->query_set().end(); it != ie; ++it) {
    if (index == sample_index*size/sample_rate) {
      sample.push_back(*it);
      while (index == sample_index*size/sample_rate) {
        sample_index++;
      }
    }
    index++;
  }
}


uint64_t BaseAggregator::GetQueryDepth(const data::SolverQuery &query) const {
  assert(query.has_execution_state() && "Cannot parse query");

  switch (path_metric_) {
  case INSTRUCTIONS:
    return query.execution_state().instructions();
  case BRANCHES:
    return query.execution_state().branches();
  case FORKS:
    return query.execution_state().forks();
  case QUERIES:
    return query.execution_state().queries();
  case TIME:
    return query.execution_state().total_time();
  }
}


void PathSensitiveDistribution::PrintStatistics(std::ostream &os) {
  for (ExecutionTraces::const_iterator it = execution_traces().begin(),
      ie = execution_traces().end(); it != ie; ++it) {
#if 1
    if (GetTraceLength(it->second) <= 100)
      continue;
#endif

    SolverQueryVector sample;
    SampleTrace(it->second, it->second->query_set().size(), sample);

    for (SolverQueryVector::iterator sit = sample.begin(),
        sie = sample.end(); sit != sie; ++sit) {
      os << GetQueryDepth(*(*sit)) << " " <<
          (*sit)->execution_state().total_time() << std::endl;
    }
    os << std::endl;
  }
}


} /* namespace klee */
