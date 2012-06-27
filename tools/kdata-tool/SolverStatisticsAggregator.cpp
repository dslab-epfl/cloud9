/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#include "SolverStatisticsAggregator.h"

#include "SolverStatistics.h"

#include "llvm/Support/CommandLine.h"

#include <iomanip>
#include <stdint.h>
#include <map>
#include <set>

using namespace llvm;

namespace {
cl::opt<bool> SolveTimeDistribution("print-solve-time-disitribution",
                                         cl::desc("Print solving time distribution"),
                                         cl::init(true));
cl::opt<bool> CodeDistribution("print-code-distribution",
                                    cl::desc("Print code distribution stats"),
                                    cl::init(false));
cl::opt<bool> ReasonDistribution("print-reason-distribution",
                                    cl::desc("Print query reason distribution"),
                                    cl::init(true));
cl::opt<bool> OperationDistribution("print-operation-distribution",
                                    cl::desc("Print query operation distribution"),
                                    cl::init(true));
}

namespace {

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


void SolverStatisticsAggregator::ProcessSolverQuery(
    const data::SolverQuery &query) {
  data::SolverQuery *query_copy = query_set_.add_solver_query();
  query_copy->CopyFrom(query);
}


void SolverStatisticsAggregator::PrintStatistics(std::ostream &os) {
  os << "[Global]" << std::endl;
  PrintBasicStatistics(os, query_set_);
  os << std::endl;

  PrintShadowStatistics(os, query_set_);

  if (SolveTimeDistribution)
    PrintSolvingTimeDistribution(os, query_set_);

  if (ReasonDistribution)
    PrintReasonDistribution(os, query_set_);

  if (OperationDistribution)
    PrintOperationDistribution(os, query_set_);

  if (CodeDistribution)
    PrintCodeLocationDistribution(os, query_set_);
}


void SolverStatisticsAggregator::PrettyPrintUsecTime(std::ostream &os, uint64_t time) {
  if (time >= 1000000UL) {
    uint64_t time_sec = time / 1000000UL;
    if (time_sec >= 60) {
      uint64_t time_min = time_sec / 60;
      if (time_min >= 60) {
        os << time_min / 60 << "h ";
      }
      os << (time_min % 60) << "min ";
    }
    os << (time_sec % 60) << "sec ";
  }
  uint64_t time_us = time % 1000000UL;
  os << (time_us / 1000) << "." << std::setfill('0') << std::setw(3) << (time_us % 1000) << "ms ";
  os << "(" << time << " us)";
}


void SolverStatisticsAggregator::PrettyPrintQueryReason(
    std::ostream &os, data::QueryReason reason) {
  switch (reason) {
  case data::OTHER:
    os << "Other";
    break;
  case data::BRANCH_FEASIBILITY:
    os << "BranchFeasibility";
    break;
  case data::SWITCH_FEASIBILITY:
    os << "SwitchFeasibility";
    break;
  case data::EXPRESSION_CONCRETIZATION:
    os << "ExpressionConcretization";
    break;
  case data::BRANCH_CONDITION_CONCRETIZATION:
    os << "BranchConditionConcretization";
    break;
  case data::EXTERNAL_CALL_CONCRETIZATION:
    os << "ExternalCallConcretization";
    break;
  case data::CHECK_UNIQUENESS:
    os << "CheckUniqueness";
    break;
  case data::CHECK_ASSUMPTION:
    os << "CheckAssumption";
    break;
  case data::USER_GET_VALUE:
    os << "UserGetValue";
    break;
  case data::SINGLE_ADDRESS_RESOLUTION:
    os << "SingleAddressResolution";
    break;
  case data::MULTI_ADDRESS_RESOLUTION:
    os << "MultiAddressResolution";
    break;
  case data::FUNCTION_RESOLUTION:
    os << "FunctionResolution";
    break;
  case data::ALLOC_RANGE_CHECK:
    os << "AllocRangeCheck";
    break;
  case data::TEST_CASE_GENERATION:
    os << "TestCaseGeneration";
    break;
  }
}

void SolverStatisticsAggregator::PrettyPrintQueryOperation(
    std::ostream &os, data::QueryOperation operation) {
  switch (operation) {
  case data::EVALUATE:
    os << "Evaluate";
    break;
  case data::MUST_BE_TRUE:
    os << "MustBeTrue";
    break;
  case data::MUST_BE_FALSE:
    os << "MustBeFalse";
    break;
  case data::MAY_BE_TRUE:
    os << "MayBeTrue";
    break;
  case data::MAY_BE_FALSE:
    os << "MayBeFalse";
    break;
  case data::GET_VALUE:
    os << "GetValue";
    break;
  case data::GET_INITIAL_VALUES:
    os << "GetInitialValues";
    break;
  case data::GET_RANGE:
    os << "GetRange";
    break;
  }
}


void SolverStatisticsAggregator::PrintBasicStatistics(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  SolverStatistics solver_stats;
  solver_stats.ComputeStatistics(query_set);

  os << "TotalQueries: " << solver_stats.total_queries() << std::endl;
  os << "TotalSolvingTime: ";
  PrettyPrintUsecTime(os, solver_stats.total_solving_time());
  os << std::endl;

  os << "MaxSolvingTime: ";
  if (solver_stats.total_queries() > 0)
    PrettyPrintUsecTime(os, solver_stats.max_solving_time());
  else
    os << "N/A";
  os << std::endl;

  os << "MinSolvingTime: ";
  if (solver_stats.total_queries() > 0)
    PrettyPrintUsecTime(os, solver_stats.min_solving_time());
  else
    os << "N/A";
  os << std::endl;

  os << "AverageSolvingTime: ";
  if (solver_stats.total_queries() > 0) {
    uint64_t average_solving_time =
        solver_stats.total_solving_time() / solver_stats.total_queries();
    PrettyPrintUsecTime(os, average_solving_time);
  } else {
    os << "N/A";
  }
  os << std::endl;


  os << "MedianSolvingTime: ";
  if (solver_stats.total_queries() > 0) {
    PrettyPrintUsecTime(os, solver_stats.median_solving_time());
  } else {
    os << "N/A";
  }
  os << std::endl;
}


void SolverStatisticsAggregator::PrintShadowStatistics(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  data::SolverQuerySet shadow_set;

  for (int i = 0; i < query_set.solver_query_size(); i++) {
    const data::SolverQuery &query = query_set.solver_query(i);

    if (!query.shadow())
      continue;

    data::SolverQuery *query_copy = shadow_set.add_solver_query();
    query_copy->CopyFrom(query);
  }

  if (shadow_set.solver_query_size() == 0)
    return;

  os << "[Shadow]" << std::endl;
  PrintBasicStatistics(os, shadow_set);
  os << std::endl;
}


void SolverStatisticsAggregator::PrintSolvingTimeDistribution(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  std::map<int, data::SolverQuerySet> distribution;

  for (int i = 0; i < query_set.solver_query_size(); i++) {
    const data::SolverQuery &query = query_set.solver_query(i);

    int time_range = ComputeLogTimeRange(query.solving_time());
    data::SolverQuery *query_copy = distribution[time_range].add_solver_query();
    query_copy->CopyFrom(query);
  }

  for (std::map<int, data::SolverQuerySet>::iterator it = distribution.begin(),
      ie = distribution.end(); it != ie; ++it) {
    os << "[SolvingRange: ";
    PrettyPrintUsecTime(os, ComputeExpTime(it->first));
    os << " - ";
    PrettyPrintUsecTime(os, ComputeExpTime(it->first+1));
    os << "]" << std::endl;
    PrintBasicStatistics(os, it->second);
    os << std::endl;
  }
}


void SolverStatisticsAggregator::PrintReasonDistribution(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  std::map<data::QueryReason, data::SolverQuerySet> distribution;

  for (int i = 0; i < query_set.solver_query_size(); ++i) {
    const data::SolverQuery &query = query_set.solver_query(i);

    data::SolverQuery *query_copy = distribution[query.reason()].add_solver_query();
    query_copy->CopyFrom(query);
  }

  for (std::map<data::QueryReason, data::SolverQuerySet>::iterator it = distribution.begin(),
      ie = distribution.end(); it != ie; ++it) {
    os << "[QueryReason: ";
    PrettyPrintQueryReason(os, it->first);
    os << "]" << std::endl;
    PrintBasicStatistics(os, it->second);
    os << std::endl;
  }
}


void SolverStatisticsAggregator::PrintOperationDistribution(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  std::map<data::QueryOperation, data::SolverQuerySet> distribution;

  for (int i = 0; i < query_set.solver_query_size(); ++i) {
    const data::SolverQuery &query = query_set.solver_query(i);

    data::SolverQuery *query_copy = distribution[query.operation()].add_solver_query();
    query_copy->CopyFrom(query);
  }

  for (std::map<data::QueryOperation, data::SolverQuerySet>::iterator it = distribution.begin(),
      ie = distribution.end(); it != ie; ++it) {
    os << "[QueryOperation: ";
    PrettyPrintQueryOperation(os, it->first);
    os << "]" << std::endl;
    PrintBasicStatistics(os, it->second);
    os << std::endl;
  }
}


void SolverStatisticsAggregator::PrintCodeLocationDistribution(std::ostream &os,
    const data::SolverQuerySet &query_set) {
  std::map<int, data::SolverQuerySet> distribution;

  for (int i = 0; i < query_set.solver_query_size(); ++i) {
    const data::SolverQuery &query = query_set.solver_query(i);

    int assembly_line = -1;
    if (query.debug_info().has_assembly_number()) {
      assembly_line = query.debug_info().assembly_number();
    }

    data::SolverQuery *query_copy = distribution[assembly_line].add_solver_query();
    query_copy->CopyFrom(query);
  }

  for (std::map<int, data::SolverQuerySet>::iterator it = distribution.begin(),
        ie = distribution.end(); it != ie; ++it) {
    os << "[AssemblyLine: " << it->first << "]" << std::endl;
    PrintBasicStatistics(os, it->second);
    os << std::endl;
  }
}
