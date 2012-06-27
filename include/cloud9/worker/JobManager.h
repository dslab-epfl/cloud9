/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
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

#ifndef JOBMANAGER_H_
#define JOBMANAGER_H_

#include "cloud9/worker/TreeNodeInfo.h"
#include "cloud9/worker/SymbolicEngine.h"
#include "cloud9/worker/CoreStrategies.h"

#include "klee/KleeHandler.h"

#include "klee/KleeHandler.h"

#include <glog/logging.h>

#include <boost/thread.hpp>
#include <list>
#include <set>
#include <string>

namespace llvm {
class Module;
class Function;
}

namespace klee {
class Interpreter;
class ExecutionState;
class Searcher;
class KModule;
}

namespace cloud9 {

namespace worker {

class SymbolicState;
class ExecutionJob;
class OracleStrategy;

class JobManager: public StateEventHandler {
private:
  /***************************************************************************
   * Initialization
   **************************************************************************/
  void initialize(llvm::Module *module, llvm::Function *mainFn, int argc,
      char **argv, char **envp);

  void initKlee();
  void initBreakpoints();
  void initStatistics();
  void initStrategy();

  StateSelectionStrategy *createCoverageOptimizedStrat(StateSelectionStrategy *base);
  OracleStrategy *createOracleStrategy();

  void initRootState(llvm::Function *f, int argc, char **argv, char **envp);

  /***************************************************************************
   * KLEE integration
   **************************************************************************/
  klee::Interpreter *interpreter;
  SymbolicEngine *symbEngine;

  klee::KleeHandler *kleeHandler;
  klee::KModule *kleeModule;

  llvm::Function *mainFn;

  /*
   * Symbolic tree
   */
  WorkerTree* tree;

  boost::condition_variable jobsAvailabe;
  boost::mutex jobsMutex;
  bool terminationRequest;

  unsigned int jobCount;

  JobSelectionStrategy *selStrategy;

  /*
   * Job execution state
   */
  ExecutionJob *currentJob;
  SymbolicState *currentState;
  bool replaying;
  bool batching;

  /*
   * Statistics
   */

  std::set<WorkerTree::NodePin> stats;
  bool statChanged;
  bool refineStats;

  /*
   * Breakpoint management data structures
   *
   */
  std::set<WorkerTree::NodePin> pathBreaks;
  std::set<unsigned int> codeBreaks;

  /*
   * Debugging and instrumentation
   */
  int traceCounter;

  bool collectTraces;

  void serializeInstructionTrace(std::ostream &s, WorkerTree::Node *node);
  void parseInstructionTrace(std::istream &s, std::vector<unsigned int> &dest);

  void serializeExecutionTrace(std::ostream &os, WorkerTree::Node *node);
  void serializeExecutionTrace(std::ostream &os, SymbolicState *state);

  void processTestCase(SymbolicState *state);

  void fireActivateState(SymbolicState *state);
  void fireDeactivateState(SymbolicState *state);
  void fireUpdateState(SymbolicState *state, WorkerTree::Node *oldNode);
  void fireStepState(SymbolicState *state);
  void fireAddJob(ExecutionJob *job);
  void fireRemovingJob(ExecutionJob *job);

  void submitJob(ExecutionJob* job, bool activateStates);
  void finalizeJob(ExecutionJob *job, bool deactivateStates, bool invalid);

  template<typename JobIterator>
  void submitJobs(JobIterator begin, JobIterator end, bool activateStates) {
    int count = 0;
    for (JobIterator it = begin; it != end; it++) {
      submitJob(*it, activateStates);
      count++;
    }

    jobsAvailabe.notify_all();

    //CLOUD9_DEBUG("Submitted " << count << " jobs to the local queue");
  }

  ExecutionJob* selectNextJob(boost::unique_lock<boost::mutex> &lock,
      unsigned int timeOut);
  ExecutionJob* selectNextJob();

  static bool isJob(WorkerTree::Node *node);
  bool isExportableJob(WorkerTree::Node *node);
  bool isValidJob(WorkerTree::Node *node);

  void executeJob(boost::unique_lock<boost::mutex> &lock, ExecutionJob *job,
      bool spawnNew);
  void executeJobsBatch(boost::unique_lock<boost::mutex> &lock,
      ExecutionJob *origJob, bool spawnNew);

  long stepInNode(boost::unique_lock<boost::mutex> &lock,
      WorkerTree::Node *node, long count);
  void replayPath(boost::unique_lock<boost::mutex> &lock,
      WorkerTree::Node *pathEnd,
      WorkerTree::Node *&brokenEnd);
  void cleanInvalidJobs(WorkerTree::Node *rootNode);

  void processLoop(bool allowGrowth, bool blocking, unsigned int timeOut);

  void refineStatistics();
  void cleanupStatistics();

  void selectJobs(WorkerTree::Node *root, std::vector<ExecutionJob*> &jobSet,
      int maxCount);

  unsigned int countJobs(WorkerTree::Node *root);

  void updateTreeOnBranch(klee::ExecutionState *state,
      klee::ExecutionState *parent, int index, klee::ForkTag forkTag);
  void updateTreeOnDestroy(klee::ExecutionState *state);

  void updateCompressedTreeOnBranch(SymbolicState *state, SymbolicState *parent);
  void updateCompressedTreeOnDestroy(SymbolicState *state);

  void fireBreakpointHit(WorkerTree::Node *node);

  /*
   * Breakpoint management
   */

  void setCodeBreakpoint(int assemblyLine);
  void setPathBreakpoint(ExecutionPathPin path);
public:
  JobManager(llvm::Module *module, std::string mainFnName, int argc,
      char **argv, char **envp);
  virtual ~JobManager();

  WorkerTree *getTree() {
    return tree;
  }

  ExecutionJob* getCurrentJob() {
    return currentJob;
  }

  SymbolicState* getCurrentState() {
    return currentState;
  }

  JobSelectionStrategy *getStrategy() {
    return selStrategy;
  }

  StateSelectionStrategy *createBaseStrategy();

  void lockJobs() {
    jobsMutex.lock();
  }
  void unlockJobs() {
    jobsMutex.unlock();
  }

  unsigned getModuleCRC() const;

  void processJobs(bool standAlone, unsigned int timeOut = 0);
  void replayJobs(ExecutionPathSetPin paths, unsigned int timeOut = 0);

  void finalize();

  virtual bool onStateBranching(klee::ExecutionState *state, klee::ForkTag forkTag);
  virtual void onStateBranched(klee::ExecutionState *state,
      klee::ExecutionState *parent, int index, klee::ForkTag forkTag);
  virtual void onStateDestroy(klee::ExecutionState *state, bool silenced);
  virtual void onControlFlowEvent(klee::ExecutionState *state,
      ControlFlowEvent event);
  virtual void onDebugInfo(klee::ExecutionState *state,
      const std::string &message);
  virtual void onOutOfResources(klee::ExecutionState *destroyedState);
  virtual void onEvent(klee::ExecutionState *state,
          unsigned int type, long int value);

  /*
   * Statistics methods
   */

  void getStatisticsData(std::vector<int> &data, ExecutionPathSetPin &paths,
      bool onlyChanged);

  unsigned int getJobCount() const { return jobCount; }

  void setRefineStatistics() {
    refineStats = true;
  }

  void requestTermination() {
    terminationRequest = true;
    // Wake up the manager
    jobsAvailabe.notify_all();
  }

  /*
   * Coverage related functionality
   */

  void getUpdatedLocalCoverage(cov_update_t &data);
  void setUpdatedGlobalCoverage(const cov_update_t &data);
  uint32_t getCoverageIDCount() const;

  /*
   * Job import/export methods
   */
  void importJobs(ExecutionPathSetPin paths, std::vector<long> &replayInstrs);
  ExecutionPathSetPin exportJobs(ExecutionPathSetPin seeds,
      std::vector<int> &counts, std::vector<long> &replayInstrs);

  void dumpStateTrace(WorkerTree::Node *node);
  void dumpInstructionTrace(WorkerTree::Node *node);

  template<class Decorator>
  void dumpSymbolicTree(WorkerTree::Node *node, Decorator decorator) {
    char fileName[256];
    snprintf(fileName, 256, "treeDump%05d.txt", traceCounter);
    traceCounter++;

    LOG(INFO) << "Dumping symbolic tree in file " << fileName;

    std::ostream *os = kleeHandler->openOutputFile(fileName);
    assert(os != NULL);

    tree->dumpDotGraph(node, *os, decorator);

    delete os;
  }
};

}
}

#endif /* JOBMANAGER_H_ */
