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


#ifndef TOOLS_KDATA_TOOL_SOLVERSTATISTICSAGGREGATOR_H_
#define TOOLS_KDATA_TOOL_SOLVERSTATISTICSAGGREGATOR_H_

#include "klee/data/ConstraintSolving.pb.h"

#include <iostream>

using namespace klee;

class SolverStatisticsAggregator {
public:
  SolverStatisticsAggregator() { }

  ~SolverStatisticsAggregator() { }

  void ProcessSolverQuery(const data::SolverQuery &query);
  void PrintStatistics(std::ostream &os);

private:
  void PrettyPrintUsecTime(std::ostream &os, uint64_t time);
  void PrettyPrintQueryReason(std::ostream &os, data::QueryReason reason);
  void PrettyPrintQueryOperation(std::ostream &os, data::QueryOperation operation);

  void PrintBasicStatistics(std::ostream &os,
      const data::SolverQuerySet &query_set);
  void PrintShadowStatistics(std::ostream &os,
      const data::SolverQuerySet &query_set);
  void PrintSolvingTimeDistribution(std::ostream &os,
      const data::SolverQuerySet &query_set);
  void PrintReasonDistribution(std::ostream &os,
      const data::SolverQuerySet &query_set);
  void PrintOperationDistribution(std::ostream &os,
      const data::SolverQuerySet &query_set);
  void PrintCodeLocationDistribution(std::ostream &os,
      const data::SolverQuerySet &query_set);

  data::SolverQuerySet query_set_;
};


#endif  // TOOLS_KDATA_TOOL_SOLVERSTATISTICSAGGREGATOR_H_
