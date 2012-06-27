/*
 * Aggregators.cpp
 *
 *  Created on: Apr 25, 2012
 *      Author: bucur
 */

#include "Aggregators.h"

#include "SolverStatistics.h"

#include <assert.h>

namespace {

// TODO(bucur): Eliminate redundancy
int ComputeLogTimeRange(uint64_t time) {
  int time_range = 0;
  while (time > 0) {
    time = time/10;
    time_range += 1;
  }

  return time_range;
}

uint64_t ComputeExpTime(int time_range) {
  if (time_range == 0)
    return 0;

  uint64_t time = 1;
  while (time_range > 1) {
    time = time*10;
    time_range -= 1;
  }

  return time;
}

}

namespace klee {

////////////////////////////////////////////////////////////////////////////////
// ExecutionEffortAggregator
////////////////////////////////////////////////////////////////////////////////

void ExecutionEffortAggregator::ProcessSolverQuery(const data::SolverQuery &query) {
  assert(query.has_execution_state() && "Cannot parse query");

  uint64_t path_depth_group =
      (GetQueryDepth(query) / path_bucket_size()) * path_bucket_size();

  if (!query_grouping_.count(path_depth_group))
      query_grouping_[path_depth_group] = new PathIDGrouping();

  PathIDGrouping *path_grouping = query_grouping_[path_depth_group];
  PathIDGrouping::iterator it = path_grouping->find(query.execution_state().id());
  if (it != path_grouping->end()) {
    if (query.execution_state().total_time() < it->second)
      return;
  }
  (*path_grouping)[query.execution_state().id()] =
    query.execution_state().total_time();
}

void ExecutionEffortAggregator::PrintStatistics(std::ostream &os) {
  os << "# (Path Depth Range) (Total Effort)" << std::endl;

  for (PathDepthGrouping::iterator pit = query_grouping_.begin(),
         pie = query_grouping_.end(); pit != pie; ++pit) {
    os << pit->first << " ";
    uint64_t min_time = (uint64_t)(-1);
    uint64_t average_time = 0;
    uint64_t max_time = 0;
    for (PathIDGrouping::iterator it = pit->second->begin(),
           ie = pit->second->end(); it != ie; ++it) {
      average_time += it->second;
      if (it->second > max_time)
        max_time = it->second;
      if (it->second < min_time)
        min_time = it->second;
    }
    average_time /= pit->second->size();
    os << average_time << " " << min_time << " " << max_time << std::endl;
  }
}

////////////////////////////////////////////////////////////////////////////////
// QueryReasonAggregator
////////////////////////////////////////////////////////////////////////////////

void QueryReasonAggregator::ProcessSolverQuery(const data::SolverQuery &query) {
  assert(query.has_execution_state() && "Cannot parse query");

  uint64_t path_depth_group =
      (GetQueryDepth(query) / path_bucket_size()) * path_bucket_size();

  QueryHLReason hl_reason;
  switch (query.reason()) {
  case data::BRANCH_FEASIBILITY:
  case data::SWITCH_FEASIBILITY:
    hl_reason = HL_BRANCH_FEASIBILITY;
    break;
  case data::SINGLE_ADDRESS_RESOLUTION:
  case data::MULTI_ADDRESS_RESOLUTION:
  case data::FUNCTION_RESOLUTION:
  case data::ALLOC_RANGE_CHECK:
    hl_reason = HL_MEMORY_CHECK;
    break;
  default:
    hl_reason = HL_OTHERS;
    break;
  }

  if (!query_grouping_.count(path_depth_group))
    query_grouping_[path_depth_group] = new QueryReasonGrouping();

  data::SolverQuery *query_copy = (*query_grouping_[path_depth_group])
      [hl_reason].add_solver_query();
  query_copy->CopyFrom(query);
}

void QueryReasonAggregator::PrintStatistics(std::ostream &os) {
  os << "# (Path Depth Range) (Branch Feasibility) (Memory Ops) (Others) (TOTAL)"
     << std::endl;

  for (PathDepthGrouping::iterator pit = query_grouping_.begin(),
      pie = query_grouping_.end(); pit != pie; ++pit) {
    os << pit->first << " ";
    uint64_t total_time = 0;
    for (QueryHLReason reason = HL_BEGIN; reason < HL_END; reason++) {
      SolverStatistics solver_stats;
      solver_stats.ComputeStatistics((*pit->second)[reason]);
      os << solver_stats.total_solving_time() << " ";
      total_time += solver_stats.total_solving_time();
    }
    os << total_time << std::endl;
  }
}

////////////////////////////////////////////////////////////////////////////////
// QueryTimeAggregator
////////////////////////////////////////////////////////////////////////////////

void QueryTimeAggregator::ProcessSolverQuery(const data::SolverQuery &query) {
  data::SolverQuery *query_copy = query_set_.add_solver_query();
  query_copy->CopyFrom(query);
}

void QueryTimeAggregator::PrintStatistics(std::ostream &os) {
  std::map<int, data::SolverQuerySet> distribution;

  for (int i = 0; i < query_set_.solver_query_size(); i++) {
    const data::SolverQuery &query = query_set_.solver_query(i);

    int time_range = ComputeLogTimeRange(query.solving_time());
    data::SolverQuery *query_copy = distribution[time_range].add_solver_query();
    query_copy->CopyFrom(query);
  }

  uint64_t cumulative_time = 0;

  for (std::map<int, data::SolverQuerySet>::iterator it = distribution.begin(),
      ie = distribution.end(); it != ie; ++it) {
    os << ComputeExpTime(it->first) << " ";

    SolverStatistics solver_stats;
    solver_stats.ComputeStatistics(it->second);
    solver_stats.Print(os);

    cumulative_time += solver_stats.total_solving_time();

    os << " " << cumulative_time << std::endl;
  }
}

} /* namespace klee */
