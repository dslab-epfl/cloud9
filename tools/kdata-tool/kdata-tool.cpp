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

#include "BaseAggregator.h"
#include "Aggregators.h"
#include "SolverStatisticsAggregator.h"

#include "klee/data/Support.h"
#include "klee/data/ConstraintSolving.pb.h"

#include "llvm/Support/CommandLine.h"

#include <glog/logging.h>

#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include <assert.h>

using namespace llvm;
using namespace klee;

namespace {

enum DataProcessingType {
  GENERAL_QUERY_DISTRIBUTION,
  QUERY_REASON_DISTRIBUTION,
  QUERY_TIME_DISTRIBUTION,
  CUMULATIVE_EXECUTION_EFFORT,
  PATH_ANALYSIS,
  PATH_DISTRIBUTION
};

cl::list<std::string> SolverQueryData(cl::Positional,
    cl::desc("<input solver log files>"), cl::ZeroOrMore);

cl::opt<DataProcessingType> DataProcessing("data-processing",
    cl::desc("Type type of data processing to make"),
    cl::values(
        clEnumValN(GENERAL_QUERY_DISTRIBUTION, "qdist-general", ""),
        clEnumValN(QUERY_REASON_DISTRIBUTION, "qdist-reason", ""),
        clEnumValN(QUERY_TIME_DISTRIBUTION, "qdist-time", ""),
        clEnumValN(CUMULATIVE_EXECUTION_EFFORT, "exec-effort", ""),
        clEnumValN(PATH_ANALYSIS, "path-analysis", ""),
        clEnumValN(PATH_DISTRIBUTION, "path-distribution", ""),
        clEnumValEnd),
    cl::init(GENERAL_QUERY_DISTRIBUTION));

cl::opt<BaseAggregator::PathLengthMetric> PathLengthMetric("path-length-metric",
    cl::desc("The metric used for measuring path length"),
    cl::values(
        clEnumValN(BaseAggregator::INSTRUCTIONS, "instructions", ""),
        clEnumValN(BaseAggregator::BRANCHES, "branches", ""),
        clEnumValN(BaseAggregator::FORKS, "forks", ""),
        clEnumValN(BaseAggregator::QUERIES, "queries", ""),
        clEnumValN(BaseAggregator::TIME, "time", ""),
        clEnumValEnd),
    cl::init(BaseAggregator::BRANCHES));

cl::opt<unsigned int> PathLengthGroupingSize("path-length-grouping-size",
    cl::init(10000));

template<typename Aggregator>
void ProcessSolverQueryData(std::istream &is,
                            Aggregator &aggregator) {
  std::string message_string;

  // Read the query sets
  data::SolverQuerySet query_set;
  int query_set_counter = 0;
  int query_counter = 0;

  for (;;query_set_counter++) {
    klee::ReadNextMessage(is, message_string);
    if (message_string.empty())
      break;

    query_set.ParseFromString(message_string);

    for (int i = 0; i < query_set.solver_query_size(); i++) {
      const data::SolverQuery &query = query_set.solver_query(i);
      aggregator.ProcessSolverQuery(query);
    }
    query_counter += query_set.solver_query_size();
  }

  LOG(INFO) << "Parsed " << query_set_counter 
            << " query sets (" << query_counter << " queries)";
}

template<typename Aggregator>
void ParseSolverQueryData(const std::vector<std::string> &input_files,
                          Aggregator &aggregator) {
  for (std::vector<std::string>::const_iterator it = input_files.begin(),
      ie = input_files.end(); it != ie; ++it) {
    std::ifstream is(it->c_str());
    if (is.fail()) {
      LOG(FATAL) << "Cannot open solver query data file '"
          << *it << "'";
    }
    LOG(INFO) << "Processing '" << *it << "'";
    ProcessSolverQueryData(is, aggregator);
    is.close();
  }

  aggregator.PrintStatistics(std::cout);
}

}


int main(int argc, char **argv, char **envp) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  google::InitGoogleLogging(argv[0]);
  cl::ParseCommandLineOptions(argc, argv, "Constraint data file processor");

  std::vector<std::string> input_files;
  input_files.assign(SolverQueryData.begin(), SolverQueryData.end());

  switch (DataProcessing) {
  case GENERAL_QUERY_DISTRIBUTION: {
    SolverStatisticsAggregator aggregator;
    ParseSolverQueryData(input_files, aggregator);
    break;
  }
  case QUERY_REASON_DISTRIBUTION: {
    QueryReasonAggregator aggregator(PathLengthMetric, PathLengthGroupingSize);
    ParseSolverQueryData(input_files, aggregator);
    break;
  }
  case QUERY_TIME_DISTRIBUTION: {
    QueryTimeAggregator aggregator;
    ParseSolverQueryData(input_files, aggregator);
    break;
  }
  case CUMULATIVE_EXECUTION_EFFORT: {
    ExecutionEffortAggregator aggregator(PathLengthMetric, PathLengthGroupingSize);
    ParseSolverQueryData(input_files, aggregator);
    break;
  }
  case PATH_ANALYSIS: {
    PathSensitiveAnalyzer analyzer(PathLengthMetric, PathLengthGroupingSize);
    ParseSolverQueryData(input_files, analyzer);
    break;
  }
  case PATH_DISTRIBUTION: {
    PathSensitiveDistribution analyzer(PathLengthMetric, PathLengthGroupingSize);
    ParseSolverQueryData(input_files, analyzer);
    break;
  }

  }

  google::protobuf::ShutdownProtobufLibrary();
}
