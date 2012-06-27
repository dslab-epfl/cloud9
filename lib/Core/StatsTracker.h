//===-- StatsTracker.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATSTRACKER_H
#define KLEE_STATSTRACKER_H

#include "CallPathManager.h"
#include "klee/data/ConstraintSolving.pb.h"
#include "klee/data/CoverageLogs.pb.h"
#include "klee/data/Profiling.pb.h"
#include "klee/data/Statistics.pb.h"
#include "klee/data/States.pb.h"

#include <iostream>
#include <set>

namespace llvm {
  class BranchInst;
  class Function;
  class Instruction;
}

namespace klee {
  class ExecutionState;
  class Executor;  
  class InstructionInfoTable;
  class InterpreterHandler;
  struct KInstruction;
  struct KFunction;
  struct StackFrame;

  struct CoverageStats {
    unsigned total_source_lines;
    unsigned total_asm_lines;
    unsigned covered_source_lines;
    unsigned covered_asm_lines;

    CoverageStats()
      : total_source_lines(0),
        total_asm_lines(0),
        covered_source_lines(0),
        covered_asm_lines(0) { }
  };

  typedef std::set<unsigned> CoverageFocusSet;

  class StatsTracker {
    friend class WriteStatsTimer;
    friend class WriteIStatsTimer;

    Executor &executor;
    std::string objectFilename;

    std::ostream *statsFile;
    std::ostream *istatsFile;
    std::ostream *pstatsFile;
    std::ostream *detailedCoverageFile;
    std::ostream *profileFile;
    std::ostream *queriesFile;
    std::ostream *statesFile;

    double startWallTime;
    
    unsigned numBranches;
    unsigned fullBranches, partialBranches;

    CoverageStats lastGlobalCoverage;
    CoverageStats lastFocusedCoverage;
    CoverageFocusSet coverageFocus;

    CallPathManager callPathManager;

    data::SolverQuerySet currentQuerySet;
    data::StatisticSet currentStatisticsSet;

    std::set<ExecutionState*> currentStates;
    data::ExecutionStateSet currentStatesSet;

  public:
    static bool useStatistics();

  private:
    void updateStateStatistics(uint64_t addend);
    void writeStatsHeader();
    void writeStatsLine();
    void writeIStats();

    void computeDetailedCoverageSnapshot(data::CoverageInfo &coverageInfo, bool complete);
    void getCallgraphProfile(data::GlobalProfile &globalProfile);

    void writeDetailedCoverageSnapshot(bool is_header);
    void writeCallgraphProfile();
    void writeSolverQueries();
    void writeStatistics();
    void writeStates();

    void readCoverageFocus(std::istream &is);
    std::string formatCoverageInfo(unsigned total, unsigned covered);
    void recordStateUpdate(const ExecutionState &state, bool terminated,
                           bool set_stamp, data::ExecutionState *es_data);
    void flushAllStates();

  public:
    StatsTracker(Executor &_executor, std::string _objectFilename,
                 bool _updateMinDistToUncovered);
    ~StatsTracker();

    // called after a new StackFrame has been pushed (for callpath tracing)
    void framePushed(StackFrame *frame, StackFrame *parentFrame);
    void framePushed(ExecutionState &es, StackFrame *parentFrame);

    // called after a StackFrame has been popped 
    void framePopped(ExecutionState &es);

    // called when some side of a branch has been visited. it is
    // imperative that this be called when the statistics index is at
    // the index for the branch itself.
    void markBranchVisited(ExecutionState *visitedTrue, 
                           ExecutionState *visitedFalse);
    
    // called when execution is done and stats files should be flushed
    void done();

    // process stats for a single instruction step, es is the state
    // about to be stepped
    void stepInstruction(ExecutionState &es);

    /// Return time in seconds since execution start.
    double elapsed();

    void computeReachableUncovered();

    void recordSolverQuery(uint64_t time_stamp, uint64_t solving_time,
        data::QueryReason reason, data::QueryOperation operation,
        bool shadow, const ExecutionState &state);
    void updateStates(ExecutionState *current,
                      const std::set<ExecutionState*> &addedStates,
                      const std::set<ExecutionState*> &removedStates);
    void recordStats();
    void commitDetailedLogs();
    void logSummary();
  };

  uint64_t computeMinDistToUncovered(const KInstruction *ki,
                                     uint64_t minDistAtRA);

}

#endif
