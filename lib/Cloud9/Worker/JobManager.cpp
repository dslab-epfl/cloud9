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

/*
 * Implementation invariants, between two consecutive job executions, when the
 * job lock is set:
 * - Every time in the tree, there is a full frontier of symbolic states.
 *
 * - A job can be either on the frontier, or ahead of it (case in which
 * replaying needs to be done). XXX: Perform a more clever way of replay.
 *
 * - An exported job will leave the frontier intact.
 *
 * - A job import cannot happen in such a way that a job lies within the limits
 * of the frontier.
 *
 */

#include "cloud9/worker/JobManager.h"
#include "cloud9/worker/TreeObjects.h"
#include "cloud9/worker/WorkerCommon.h"
#include "cloud9/worker/KleeCommon.h"
#include "cloud9/worker/CoreStrategies.h"
#include "cloud9/worker/ComplexStrategies.h"
#include "cloud9/worker/OracleStrategy.h"
#include "cloud9/worker/FIStrategy.h"
#include "cloud9/Logger.h"
#include "cloud9/Common.h"
#include "cloud9/ExecutionTree.h"
#include "cloud9/instrum/InstrumentationManager.h"
#include "cloud9/instrum/LocalFileWriter.h"
#include "cloud9/instrum/Timing.h"

#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Instructions.h"
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 9)
#include "llvm/System/TimeValue.h"
#else
#include "llvm/Support/TimeValue.h"
#endif
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 9)
#include "llvm/System/Path.h"
#else
#include "llvm/Support/Path.h"
#endif
#include "llvm/Support/CommandLine.h"
#if !(LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
#include "llvm/Support/raw_os_ostream.h"
#endif

#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/KleeHandler.h"
#include "klee/Init.h"
#include "klee/Constants.h"

#include "../../Core/Common.h"

#include <boost/io/ios_state.hpp>
#include <boost/crc.hpp>
#include <boost/bind.hpp>
#include <stack>
#include <map>
#include <set>
#include <fstream>
#include <iostream>
#include <iomanip>

using llvm::sys::TimeValue;
using cloud9::instrum::Timer;
using namespace llvm;

namespace klee {
namespace stats {
extern Statistic locallyCoveredInstructions;
extern Statistic globallyCoveredInstructions;
extern Statistic locallyUncoveredInstructions;
extern Statistic globallyUncoveredInstructions;
}
}

namespace {
/* Job Selection Settings *****************************************************/

cl::opt<bool> StratRandom("c9-job-random", cl::desc("Use random job selection"));
cl::opt<bool> StratRandomPath("c9-job-random-path" , cl::desc("Use random path job selection"));
cl::opt<bool> StratCovOpt("c9-job-cov-opt", cl::desc("Use coverage optimized job selection"));
cl::opt<bool> StratOracle("c9-job-oracle", cl::desc("Use the almighty oracle"));
cl::opt<bool> StratFaultInj("c9-job-fault-inj", cl::desc("Use fault injection"));

/* Other Settings *************************************************************/

cl::opt<unsigned> MakeConcreteSymbolic("make-concrete-symbolic", cl::desc(
  "Rate at which to make concrete reads symbolic (0=off)"), cl::init(0));

cl::opt<bool> OptimizeModule("optimize", cl::desc("Optimize before execution"));

cl::opt<bool> CheckDivZero("check-div-zero", cl::desc(
  "Inject checks for division-by-zero"), cl::init(true));

cl::list<unsigned int> CodeBreakpoints("c9-code-bp", cl::desc(
  "Breakpoints in the LLVM assembly file"));

cl::opt<bool> BreakOnReplayBroken("c9-bp-on-replaybr", cl::desc(
  "Break on last valid position if a broken replay occurrs."));

cl::opt<bool>
  DumpStateTraces("c9-dump-traces",
  cl::desc("Dump state traces when a breakpoint or any other relevant event happens during execution"),
  cl::init(false));

cl::opt<bool>
  DumpInstrTraces("c9-dump-instr",
  cl::desc("Dump instruction traces for each finished state. Designed to be used for concrete executions."),
  cl::init(false));

cl::opt<std::string>
  OraclePath("c9-oracle-path", cl::desc("The file used by the oracle strategy to get the path."));

cl::opt<double>
  JobQuanta("c9-job-quanta", cl::desc("The maximum quantum of time for a job"),
  cl::init(1.0));

cl::opt<bool>
  InjectFaults("c9-fault-inj", cl::desc("Fork at fault injection points"),
      cl::init(false));

}

using namespace klee;

namespace cloud9 {

namespace worker {

/*******************************************************************************
 * HELPER FUNCTIONS FOR THE JOB MANAGER
 ******************************************************************************/

StateSelectionStrategy *JobManager::createCoverageOptimizedStrat(StateSelectionStrategy *base) {
  std::vector<StateSelectionStrategy*> strategies;

  strategies.push_back(new WeightedRandomStrategy(
         WeightedRandomStrategy::CoveringNew,
         tree,
         symbEngine));
  strategies.push_back(base);
  return new TimeMultiplexedStrategy(strategies);
}

bool JobManager::isJob(WorkerTree::Node *node) {
  return (**node).getJob() != NULL;
}

bool JobManager::isExportableJob(WorkerTree::Node *node) {
  ExecutionJob *job = (**node).getJob();
  if (!job)
    return false;

  if (job == currentJob)
    return false;

  return true;
}

bool JobManager::isValidJob(WorkerTree::Node *node) {
  assert(node->layerExists(WORKER_LAYER_JOBS));
  while (node != NULL) {
    if (node->layerExists(WORKER_LAYER_STATES)) {
      if ((**node).getSymbolicState() != NULL)
        return true;
      else
        return false;
    } else {
      node = node->getParent();
    }
  }

  return false;
}

void JobManager::serializeInstructionTrace(std::ostream &s,
    WorkerTree::Node *node) {
  std::vector<int> path;

  tree->buildPath(WORKER_LAYER_STATES, node, tree->getRoot(), path);

  const WorkerTree::Node *crtNode = tree->getRoot();
  unsigned int count = 0;

  bool enabled = false;

  for (unsigned int i = 0; i <= path.size(); i++) {
    const ExecutionTrace &trace = (**crtNode).getTrace();

    for (ExecutionTrace::const_iterator it = trace.getEntries().begin();
        it != trace.getEntries().end(); it++) {

      if (InstructionTraceEntry *instEntry = dyn_cast<InstructionTraceEntry>(*it)) {
        if (enabled) {
          unsigned int opcode = instEntry->getInstruction()->inst->getOpcode();
          s.write((char*)&opcode, sizeof(unsigned int));
        }
        count++;
      } else if (BreakpointEntry *brkEntry = dyn_cast<BreakpointEntry>(*it)) {
        if (!enabled && brkEntry->getID() == KLEE_BRK_START_TRACING) { // XXX This is ugly
          CLOUD9_DEBUG("Starting to serialize. Skipped " << count << " instructions.");
          enabled = true;
        }
      }
    }

    if (i < path.size()) {
      crtNode = crtNode->getChild(WORKER_LAYER_STATES, path[i]);
      assert(crtNode);
    }
  }

  CLOUD9_DEBUG("Serialized " << count << " instructions.");
}

void JobManager::parseInstructionTrace(std::istream &s,
    std::vector<unsigned int> &dest) {

  dest.clear();

  while (!s.eof()) {
    unsigned int instID;

    s.read((char*)&instID, sizeof(unsigned int));

    if (!s.fail()) {
      dest.push_back(instID);
    }
  }

  CLOUD9_DEBUG("Parsed " << dest.size() << " instructions.");
}

void JobManager::serializeExecutionTrace(std::ostream &os,
    WorkerTree::Node *node) { // XXX very slow - read the .ll file and use it instead
  std::vector<int> path;

  tree->buildPath(WORKER_LAYER_STATES, node, tree->getRoot(), path);

  WorkerTree::Node *crtNode = tree->getRoot();
  const llvm::BasicBlock *crtBasicBlock = NULL;
  const llvm::Function *crtFunction = NULL;

  llvm::raw_os_ostream raw_os(os);

  for (unsigned int i = 0; i <= path.size(); i++) {
    const ExecutionTrace &trace = (**crtNode).getTrace();
    // Output each instruction in the node
    for (ExecutionTrace::const_iterator it = trace.getEntries().begin(); it
        != trace.getEntries().end(); it++) {
      if (InstructionTraceEntry *instEntry = dyn_cast<InstructionTraceEntry>(*it)) {
        klee::KInstruction *ki = instEntry->getInstruction();
        bool newBB = false;
        bool newFn = false;

        if (ki->inst->getParent() != crtBasicBlock) {
          crtBasicBlock = ki->inst->getParent();
          newBB = true;
        }

        if (crtBasicBlock != NULL && crtBasicBlock->getParent() != crtFunction) {
          crtFunction = crtBasicBlock->getParent();
          newFn = true;
        }

        if (newFn) {
          os << std::endl;
          os << "   Function '"
              << ((crtFunction != NULL) ? crtFunction->getNameStr() : "")
              << "':" << std::endl;
        }

        if (newBB) {
          os << "----------- "
              << ((crtBasicBlock != NULL) ? crtBasicBlock->getNameStr() : "")
              << " ----" << std::endl;
        }

        boost::io::ios_all_saver saver(os);
        os << std::setw(9) << ki->info->assemblyLine << ": ";
        saver.restore();
        ki->inst->print(raw_os, NULL);
        os << std::endl;
      } else if (DebugLogEntry *logEntry = dyn_cast<DebugLogEntry>(*it)) {
        os << logEntry->getMessage() << std::endl;
      }
    }

    if (i < path.size()) {
      crtNode = crtNode->getChild(WORKER_LAYER_STATES, path[i]);
    }
  }
}

void JobManager::serializeExecutionTrace(std::ostream &os, SymbolicState *state) {
  WorkerTree::Node *node = state->getNode().get();
  serializeExecutionTrace(os, node);
}

void JobManager::processTestCase(SymbolicState *state) {
  std::vector<EventEntry*> eventEntries;

  // First, collect the event entries
  std::vector<int> path;

  tree->buildPath(WORKER_LAYER_STATES, state->getNode().get(), tree->getRoot(), path);

  const WorkerTree::Node *crtNode = tree->getRoot();

  for (unsigned int i = 0; i <= path.size(); i++) {
    const ExecutionTrace &trace = (**crtNode).getTrace();

    for (ExecutionTrace::const_iterator it = trace.getEntries().begin();
        it != trace.getEntries().end(); it++) {

      if (EventEntry *eventEntry = dyn_cast<EventEntry>(*it)) {
        eventEntries.push_back(eventEntry);
      }
    }

    if (i < path.size()) {
      crtNode = crtNode->getChild(WORKER_LAYER_STATES, path[i]);
      assert(crtNode);
    }
  }

  if (eventEntries.size() == 0)
    return;

  std::ostream *f = kleeHandler->openTestFile("events");

  if (f) {
    for (std::vector<EventEntry*>::iterator it = eventEntries.begin();
        it != eventEntries.end(); it++) {
      EventEntry *event = *it;
      *f << "Event: " << event->getType() << " Value: " << event->getValue() << std::endl;
      event->getStackTrace().dump(*f);
      *f << std::endl;
    }
    delete f;
  }
}

/*******************************************************************************
 * JOB MANAGER METHODS
 ******************************************************************************/

/* Initialization Methods *****************************************************/

JobManager::JobManager(llvm::Module *module, std::string mainFnName, int argc,
    char **argv, char **envp) :
  terminationRequest(false), jobCount(0), currentJob(NULL), replaying(false),
  batching(true), traceCounter(0) {

  tree = new WorkerTree();

  llvm::Function *mainFn = module->getFunction(mainFnName);

  collectTraces = DumpStateTraces || DumpInstrTraces;

  initialize(module, mainFn, argc, argv, envp);
}

void JobManager::initialize(llvm::Module *module, llvm::Function *_mainFn,
    int argc, char **argv, char **envp) {
  mainFn = _mainFn;
  assert(mainFn);

  klee::Interpreter::InterpreterOptions iOpts;
  iOpts.MakeConcreteSymbolic = MakeConcreteSymbolic;

  llvm::sys::Path libraryPath(getKleeLibraryPath());

  klee::Interpreter::ModuleOptions mOpts(libraryPath.c_str(),
  /*Optimize=*/OptimizeModule,
  /*CheckDivZero=*/CheckDivZero);

  kleeHandler = new klee::KleeHandler(argc, argv);
  interpreter = klee::Interpreter::create(iOpts, kleeHandler);
  kleeHandler->setInterpreter(interpreter);

  symbEngine = dyn_cast<SymbolicEngine> (interpreter);
  interpreter->setModule(module, mOpts);

  kleeModule = symbEngine->getModule();

  klee::externalsAndGlobalsCheck(kleeModule->module);

  symbEngine->registerStateEventHandler(this);

  theStatisticManager->trackChanges(stats::locallyCoveredInstructions);

  initStrategy();
  initStatistics();
  initBreakpoints();

  initRootState(mainFn, argc, argv, envp);
}

void JobManager::initRootState(llvm::Function *f, int argc, char **argv,
    char **envp) {
  klee::ExecutionState *kState = symbEngine->createRootState(f);
  SymbolicState *state = new SymbolicState(kState);

  state->rebindToNode(tree->getRoot());

  symbEngine->initRootState(kState, argc, argv, envp);
}

StateSelectionStrategy *JobManager::createBaseStrategy() {
  // Step 1: Compose the basic strategy
  StateSelectionStrategy *stateStrat = NULL;

  if (StratRandomPath) {
    stateStrat = new RandomPathStrategy(tree);
  } else {
    stateStrat = new RandomStrategy();
  }

  // Step 2: Check to see if want the coverage-optimized strategy
  if (StratCovOpt) {
    stateStrat = createCoverageOptimizedStrat(stateStrat);
  }

  return stateStrat;
}

void JobManager::initStrategy() {
  if (StratOracle)
    batching = false;

  if (StratOracle) {
    selStrategy = createOracleStrategy();
    CLOUD9_INFO("Using the oracle");
    return;
  }

  StateSelectionStrategy *stateStrat = NULL;

  stateStrat = createBaseStrategy();
  CLOUD9_INFO("Using the regular strategy stack");

  selStrategy = new RandomJobFromStateStrategy(tree, stateStrat, this);
}

OracleStrategy *JobManager::createOracleStrategy() {
  std::vector<unsigned int> goalPath;

  std::ifstream ifs(OraclePath);

  assert(!ifs.fail());

  parseInstructionTrace(ifs, goalPath);

  OracleStrategy *result = new OracleStrategy(tree, goalPath, this);

  return result;
}

void JobManager::initStatistics() {
  // Configure the root as a statistics node
  WorkerTree::NodePin rootPin = tree->getRoot()->pin(WORKER_LAYER_STATISTICS);

  stats.insert(rootPin);
  statChanged = true;
  refineStats = false;
}

void JobManager::initBreakpoints() {
  // Register code breakpoints
  for (unsigned int i = 0; i < CodeBreakpoints.size(); i++) {
    setCodeBreakpoint(CodeBreakpoints[i]);
  }
}

/* Finalization Methods *******************************************************/

JobManager::~JobManager() {
  if (symbEngine != NULL) {
    delete symbEngine;
  }

  cloud9::instrum::theInstrManager.stop();
}

void JobManager::finalize() {
  symbEngine->deregisterStateEventHandler(this);
  symbEngine->destroyStates();

  CLOUD9_INFO("Finalized job execution.");
}

/* Misc. Methods **************************************************************/

unsigned JobManager::getModuleCRC() const {
  std::string moduleContents;
  llvm::raw_string_ostream os(moduleContents);

  kleeModule->module->print(os, NULL);

  os.flush();

  boost::crc_ccitt_type crc;
  crc.process_bytes(moduleContents.c_str(), moduleContents.size());

  return crc.checksum();
}

WorkerTree::Node *JobManager::getCurrentNode() {
  if (!currentJob)
    return NULL;

  return currentJob->getNode().get();
}

/* Job Manipulation Methods ***************************************************/

void JobManager::processJobs(bool standAlone, unsigned int timeOut) {
  if (timeOut > 0) {
    CLOUD9_INFO("Processing jobs with a timeout of " << timeOut << " seconds.");
  }

  if (standAlone) {
    // We need to import the root job
    std::vector<long> replayInstrs;
    importJobs(ExecutionPathSet::getRootSet(), replayInstrs);
  }

  processLoop(true, !standAlone, timeOut);
}

void JobManager::replayJobs(ExecutionPathSetPin paths, unsigned int timeOut) {
  // First, we need to import the jobs in the manager
  std::vector<long> replayInstrs;
  importJobs(paths, replayInstrs);

  // Then we execute them, but only them (non blocking, don't allow growth),
  // until the queue is exhausted
  processLoop(false, false, timeOut);
}

void JobManager::processLoop(bool allowGrowth, bool blocking,
    unsigned int timeOut) {
  ExecutionJob *job = NULL;

  boost::unique_lock<boost::mutex> lock(jobsMutex);

  TimeValue deadline = TimeValue::now() + TimeValue(timeOut, 0);

  while (!terminationRequest) {
    TimeValue now = TimeValue::now();

    if (timeOut > 0 && now > deadline) {
      CLOUD9_INFO("Timeout reached. Suspending execution.");
      break;
    }

    if (blocking) {
      if (timeOut > 0) {
        TimeValue remaining = deadline - now;

        job = selectNextJob(lock, remaining.seconds());
      } else {
        job = selectNextJob(lock, 0);
      }
    } else {
      job = selectNextJob();
    }

    if (blocking && timeOut == 0 && !terminationRequest) {
      assert(job != NULL);
    } else {
      if (job == NULL)
        break;
    }

    executeJobsBatch(lock, job, allowGrowth);

    if (refineStats) {
      refineStatistics();
      refineStats = false;
    }
  }

  if (terminationRequest)
    CLOUD9_INFO("Termination was requested.");
}

ExecutionJob* JobManager::selectNextJob(boost::unique_lock<boost::mutex> &lock,
    unsigned int timeOut) {
  ExecutionJob *job = selectNextJob();
  assert(job != NULL || jobCount == 0);

  while (job == NULL && !terminationRequest) {
    CLOUD9_INFO("No jobs in the queue, waiting for...");
    cloud9::instrum::theInstrManager.recordEvent(
        cloud9::instrum::JobExecutionState, "idle");

    bool result = true;
    if (timeOut > 0)
      result = jobsAvailabe.timed_wait(lock,
          boost::posix_time::seconds(timeOut));
    else
      jobsAvailabe.wait(lock);

    if (!result) {
      CLOUD9_INFO("Timeout while waiting for new jobs. Aborting.");
      return NULL;
    } else
      CLOUD9_INFO("More jobs available. Resuming exploration...");

    job = selectNextJob();

    if (job != NULL)
      cloud9::instrum::theInstrManager.recordEvent(
          cloud9::instrum::JobExecutionState, "working");
  }

  return job;
}

ExecutionJob* JobManager::selectNextJob() {
  ExecutionJob *job = selStrategy->onNextJobSelection();
  if (!StratOracle)
    assert(job != NULL || jobCount == 0);

  return job;
}

void JobManager::submitJob(ExecutionJob* job, bool activateStates) {
  WorkerTree::Node *node = job->getNode().get();
  assert((**node).symState || job->isImported());

  fireAddJob(job);

  if (activateStates) {
    // Check for the state on the supporting branch
    while (node) {
      SymbolicState *state = (**node).getSymbolicState();

      if (state) {
        if (!state->_active) {
          cloud9::instrum::theInstrManager.decStatistic(
              cloud9::instrum::TotalWastedInstructions,
              state->_instrSinceFork);
        }
        fireActivateState(state);
        break;
      }

      node = node->getParent();
    }
  }

  jobCount++;

}

void JobManager::finalizeJob(ExecutionJob *job, bool deactivateStates, bool invalid) {
  WorkerTree::Node *node = job->getNode().get();

  job->removing = true;

  if (deactivateStates) {
    while (node) {
      if (node->getCount(WORKER_LAYER_JOBS) > 1)
        break; // We reached a junction

      SymbolicState *state = (**node).getSymbolicState();

      if (state) {
        fireDeactivateState(state);
        cloud9::instrum::theInstrManager.incStatistic(
            cloud9::instrum::TotalWastedInstructions,
            state->_instrSinceFork);
        break;
      }

      node = node->getParent();
    }
  }

  fireRemovingJob(job);
  delete job;

  jobCount--;
  if (invalid) {
    cloud9::instrum::theInstrManager.incStatistic(cloud9::instrum::TotalDroppedJobs);
  }
}

void JobManager::selectJobs(WorkerTree::Node *root,
    std::vector<ExecutionJob*> &jobSet, int maxCount) {

  std::vector<WorkerTree::Node*> nodes;

  tree->getLeaves(WORKER_LAYER_JOBS, root, boost::bind(
      &JobManager::isExportableJob, this, _1), maxCount, nodes);

  for (std::vector<WorkerTree::Node*>::iterator it = nodes.begin(); it
      != nodes.end(); it++) {
    WorkerTree::Node* node = *it;
    jobSet.push_back((**node).getJob());
  }

  CLOUD9_DEBUG("Selected " << jobSet.size() << " jobs");

}

unsigned int JobManager::countJobs(WorkerTree::Node *root) {
  return tree->countLeaves(WORKER_LAYER_JOBS, root, &isJob);
}

void JobManager::importJobs(ExecutionPathSetPin paths,
    std::vector<long> &replayInstrs) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  std::vector<WorkerTree::Node*> nodes;
  std::vector<ExecutionJob*> jobs;

  tree->getNodes(WORKER_LAYER_JOBS, paths, nodes,
      (std::map<unsigned,WorkerTree::Node*>*)NULL);

  CLOUD9_DEBUG("Importing " << paths->count() << " jobs");

  unsigned droppedCount = 0;

  for (unsigned int i = 0; i < nodes.size(); i++) {
    WorkerTree::Node *crtNode = nodes[i];
    assert(crtNode->getCount(WORKER_LAYER_JOBS) == 0
        && "Job duplication detected");
    assert((!crtNode->layerExists(WORKER_LAYER_STATES) || crtNode->getCount(WORKER_LAYER_STATES) == 0)
        && "Job before the state frontier");

    if (crtNode->getCount(WORKER_LAYER_JOBS) > 0) {
      CLOUD9_INFO("BUG! Discarding job as being obsolete: " << *crtNode);
    } else {
      // The exploration job object gets a pin on the node, thus
      // ensuring it will be released automatically after it's no
      // longer needed
      ExecutionJob *job = new ExecutionJob(crtNode, true);

      if (replayInstrs.size() > 0)
        job->replayInstr = replayInstrs[i];

      if (!isValidJob(crtNode)) {
        droppedCount++;

        delete job;
      } else {
        jobs.push_back(job);
      }
    }
  }

  if (droppedCount > 0) {
    CLOUD9_DEBUG("BUG! " << droppedCount << " jobs dropped before being imported.");
  }

  submitJobs(jobs.begin(), jobs.end(), true);

  cloud9::instrum::theInstrManager.incStatistic(
      cloud9::instrum::TotalImportedJobs, jobs.size());
  cloud9::instrum::theInstrManager.incStatistic(
      cloud9::instrum::TotalTreePaths, jobs.size());
}

ExecutionPathSetPin JobManager::exportJobs(ExecutionPathSetPin seeds,
    std::vector<int> &counts, std::vector<long> &replayInstrs) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  std::vector<WorkerTree::Node*> roots;
  std::vector<ExecutionJob*> jobs;
  std::vector<WorkerTree::Node*> jobRoots;

  tree->getNodes(WORKER_LAYER_JOBS, seeds, roots,
      (std::map<unsigned,WorkerTree::Node*>*)NULL);

  assert(roots.size() == counts.size());

  for (unsigned int i = 0; i < seeds->count(); i++) {
    selectJobs(roots[i], jobs, counts[i]);
  }

  replayInstrs.clear();

  for (std::vector<ExecutionJob*>::iterator it = jobs.begin(); it != jobs.end(); it++) {
    ExecutionJob *job = *it;
    WorkerTree::Node *node = job->getNode().get();

    jobRoots.push_back(node);

    if ((**node).symState != NULL) {
      replayInstrs.push_back((**node).symState->_instrSinceFork);
    } else {
      replayInstrs.push_back(job->replayInstr);
    }
  }

  // Do this before de-registering the jobs, in order to keep the nodes pinned
  ExecutionPathSetPin paths = tree->buildPathSet(jobRoots.begin(),
      jobRoots.end(), (std::map<WorkerTree::Node*,unsigned>*)NULL);

  // De-register the jobs with the worker
  for (std::vector<ExecutionJob*>::iterator it = jobs.begin(); it != jobs.end(); it++) {
    // Cancel each job
    ExecutionJob *job = *it;
    assert(currentJob != job);

    job->exported = true;
    job->removing = true;

    finalizeJob(job, true, false);
  }

  cloud9::instrum::theInstrManager.incStatistic(
      cloud9::instrum::TotalExportedJobs, paths->count());
  cloud9::instrum::theInstrManager.decStatistic(
      cloud9::instrum::TotalTreePaths, paths->count());

  return paths;
}

/* Strategy Handler Triggers *************************************************/

void JobManager::fireActivateState(SymbolicState *state) {
  if (!state->_active) {
    state->_active = true;

    selStrategy->onStateActivated(state);
    cloud9::instrum::theInstrManager.incStatistic(
        cloud9::instrum::CurrentActiveStateCount);
  }
}

void JobManager::fireDeactivateState(SymbolicState *state) {
  if (state->_active) {
    state->_active = false;

    selStrategy->onStateDeactivated(state);
    cloud9::instrum::theInstrManager.decStatistic(
        cloud9::instrum::CurrentActiveStateCount);
  }
}

void JobManager::fireUpdateState(SymbolicState *state, WorkerTree::Node *oldNode) {
  if (state->_active) {
    selStrategy->onStateUpdated(state, oldNode);
  }
}

void JobManager::fireAddJob(ExecutionJob *job) {
  selStrategy->onJobAdded(job);
  cloud9::instrum::theInstrManager.incStatistic(
      cloud9::instrum::CurrentJobCount);
}

void JobManager::fireRemovingJob(ExecutionJob *job) {
  selStrategy->onRemovingJob(job);
  cloud9::instrum::theInstrManager.decStatistic(
      cloud9::instrum::CurrentJobCount);
}

/* Job Execution Methods ******************************************************/

void JobManager::executeJobsBatch(boost::unique_lock<boost::mutex> &lock,
      ExecutionJob *origJob, bool spawnNew) {
  Timer timer;
  timer.start();

  WorkerTree::NodePin nodePin = origJob->getNode();

  if ((**nodePin).getSymbolicState() == NULL) {
    // Replay job
    executeJob(lock, origJob, spawnNew);
    return;
  }

  double startTime = klee::util::getUserTime();
  double currentTime = startTime;

  unsigned int count = 0;

  do {
    executeJob(lock, (**nodePin).getJob(), spawnNew);
    count++;

    if ((**nodePin).getJob() == NULL) {
      WorkerTree::Node *newNode = tree->selectRandomLeaf(WORKER_LAYER_JOBS, nodePin.get(), theRNG);

      if ((**newNode).getJob() == NULL)
        break;

      nodePin = (**newNode).getJob()->getNode();
    }

    currentTime = klee::util::getUserTime();
  } while (batching && currentTime - startTime < JobQuanta);

  timer.stop();

  cloud9::instrum::theInstrManager.recordEvent(
      cloud9::instrum::InstructionBatch, timer);

  //CLOUD9_DEBUG("Batched " << count << " jobs");
}

void JobManager::executeJob(boost::unique_lock<boost::mutex> &lock,
    ExecutionJob *job, bool spawnNew) {
  WorkerTree::NodePin nodePin = job->getNode(); // Keep the node around until we finish with it
  WorkerTree::Node *brokenNode = NULL;

  currentJob = job;

  if ((**nodePin).symState == NULL) {
    if (!job->isImported()) {
      CLOUD9_INFO("Replaying path for non-foreign job. Most probably this job will be lost.");
    }

    cloud9::instrum::theInstrManager.recordEvent(
        cloud9::instrum::JobExecutionState, "startReplay");

    replayPath(lock, nodePin.get(), brokenNode);

    cloud9::instrum::theInstrManager.recordEvent(
        cloud9::instrum::JobExecutionState, "endReplay");

    cloud9::instrum::theInstrManager.incStatistic(
        cloud9::instrum::TotalReplayedJobs);
  } else {
    if (job->isImported()) {
      CLOUD9_INFO("Foreign job with no replay needed. Probably state was obtained through other neighbor replays.");
    }
  }

  job->imported = false;

  if ((**nodePin).symState == NULL) {
    assert(brokenNode != NULL);

    CLOUD9_INFO("Job canceled before start");
  } else {
    if (job->replayInstr > 0) {
      long done = (**nodePin).symState->_instrSinceFork;

      if (job->replayInstr - done > 0) {
        long count = stepInNode(lock, nodePin.get(), job->replayInstr - done);
        if (count < job->replayInstr - done) {
          CLOUD9_DEBUG("BUG: Replayed " << count << " instructions instead of " << (job->replayInstr - done));
        } else {
          CLOUD9_DEBUG("Replayed " << count << " instructions");
        }

        cloud9::instrum::theInstrManager.incStatistic(cloud9::instrum::TotalReplayInstructions, count);
      } else {
        stepInNode(lock, nodePin.get(), 1);
      }

      job->replayInstr = 0;
    } else {
      stepInNode(lock, nodePin.get(), 1);
    }
  }

  currentJob = NULL;

  if ((**nodePin).symState != NULL) {
    // Just mark the state as updated, no node progress
    fireUpdateState((**nodePin).symState, nodePin.get());
  } else if (brokenNode != NULL) {
    cleanInvalidJobs(brokenNode);
  }
}

void JobManager::cleanInvalidJobs(WorkerTree::Node *rootNode) {
  std::vector<WorkerTree::Node*> jobNodes;

  tree->getLeaves(WORKER_LAYER_JOBS, rootNode, jobNodes);

  CLOUD9_DEBUG("BUG! Cleaning " << jobNodes.size() << " broken jobs.");

  for (std::vector<WorkerTree::Node*>::iterator it = jobNodes.begin();
      it != jobNodes.end(); it++) {
    WorkerTree::Node *node = *it;
    assert(!node->layerExists(WORKER_LAYER_STATES));

    ExecutionJob *job = (**node).job;
    assert(job != NULL);

    finalizeJob(job, false, true);
  }

}

long JobManager::stepInNode(boost::unique_lock<boost::mutex> &lock,
    WorkerTree::Node *node, long count) {
  assert((**node).symState != NULL);

  // Keep the node alive until we finish with it
  WorkerTree::NodePin nodePin = node->pin(WORKER_LAYER_STATES);

  long totalExec = 0;

  while ((**node).symState != NULL) {

    SymbolicState *state = (**node).symState;

    if (!codeBreaks.empty()) {
      if (codeBreaks.find(state->getKleeState()->pc()->info->assemblyLine)
          != codeBreaks.end()) {
        // We hit a breakpoint
        fireBreakpointHit(node);
      }
    }

    //if (replaying)
    //  fprintf(stderr, "%d ", state->getKleeState()->pc()->info->assemblyLine);

    if (state->collectProgress) {
      state->_instrProgress.push_back(state->getKleeState()->pc());
    }

    // Execute the instruction
    lock.unlock();

    symbEngine->stepInState(state->getKleeState());

    lock.lock();

    state->_instrSinceFork++;
    totalExec++;
    cloud9::instrum::theInstrManager.incStatistic(
        cloud9::instrum::TotalProcInstructions);
    if (replaying)
      cloud9::instrum::theInstrManager.incStatistic(
          cloud9::instrum::TotalReplayInstructions);

    if (count == 1) {
      break;
    } else if (count > 1) {
      count--;
    }
  }

  return totalExec;
}

void JobManager::replayPath(boost::unique_lock<boost::mutex> &lock,
    WorkerTree::Node *pathEnd, WorkerTree::Node *&brokenEnd) {
  Timer timer;
  timer.start();

  std::vector<int> path;

  WorkerTree::Node *crtNode = pathEnd;
  brokenEnd = NULL;

  //CLOUD9_DEBUG("Replaying path: " << *crtNode);

  while (crtNode != NULL && (**crtNode).symState == NULL) {
    path.push_back(crtNode->getIndex());

    crtNode = crtNode->getParent();
  }

  assert(crtNode != NULL);

  std::reverse(path.begin(), path.end());

  //CLOUD9_DEBUG("Started path replay at position: " << *crtNode);

  replaying = true;

  // Perform the replay work
  for (unsigned int i = 0; i < path.size(); i++) {
    if (!crtNode->layerExists(WORKER_LAYER_STATES)) {
      if (crtNode->getParent() && crtNode->getParent()->layerExists(WORKER_LAYER_STATES) &&
          crtNode->getParent()->getChild(WORKER_LAYER_STATES, 1-crtNode->getIndex())) {
        CLOUD9_DEBUG("Replay broken because of different branch taken");

        WorkerTree::Node *node = crtNode->getParent()->getChild(WORKER_LAYER_STATES, 1-crtNode->getIndex());
        if ((**node).getSymbolicState()) {
          (**node).getSymbolicState()->getKleeState()->getStackTrace().dump(std::cout);
        }
      }
      // We have a broken replay
      break;
    }



    if ((**crtNode).symState != NULL) {
      stepInNode(lock, crtNode, -1);
    }

    if (crtNode->getChild(WORKER_LAYER_JOBS, 1-path[i]) &&
        !crtNode->getChild(WORKER_LAYER_STATES, 1-path[i])) {
      // Broken replays...
      cleanInvalidJobs(crtNode->getChild(WORKER_LAYER_JOBS, 1-path[i]));
    }

    crtNode = crtNode->getChild(WORKER_LAYER_JOBS, path[i]);
    assert(crtNode != NULL);
  }

  replaying = false;

  if (!crtNode->layerExists(WORKER_LAYER_STATES)) {
    assert((**crtNode).symState == NULL);
    CLOUD9_ERROR("Replay broken, NULL state at the end of the path.");

    brokenEnd = crtNode;

    if (BreakOnReplayBroken) {
      fireBreakpointHit(crtNode->getParent());
    }
  } else {
    assert((**crtNode).symState != NULL);
  }

  timer.stop();
  cloud9::instrum::theInstrManager.recordEvent(cloud9::instrum::ReplayBatch, timer);
}

/* Symbolic Engine Callbacks **************************************************/

bool JobManager::onStateBranching(klee::ExecutionState *state, klee::ForkTag forkTag) {
  switch (forkTag.forkClass) {
  case KLEE_FORK_FAULTINJ:
    return StratFaultInj;
  default:
    return false;
  }
}

void JobManager::onStateBranched(klee::ExecutionState *kState,
    klee::ExecutionState *parent, int index, klee::ForkTag forkTag) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  assert(parent);
  assert(kState);

  //if (kState)
  //	CLOUD9_DEBUG("State branched: " << parent->getCloud9State()->getNode());

  WorkerTree::NodePin pNode = parent->getCloud9State()->getNode();

  updateTreeOnBranch(kState, parent, index, forkTag);

  SymbolicState *state = kState->getCloud9State();

  if (parent->getCloud9State()->collectProgress) {
    state->collectProgress = true;
    state->_instrProgress = parent->getCloud9State()->_instrProgress; // XXX This is totally inefficient
    state->_instrPos = parent->getCloud9State()->_instrPos;
  }

  if (state->getNode()->layerExists(WORKER_LAYER_JOBS) || !replaying) {
    fireActivateState(state);
  }

  //CLOUD9_DEBUG("State forked at level " << state->getNode()->getLevel());

  SymbolicState *pState = parent->getCloud9State();

  if (pState->getNode()->layerExists(WORKER_LAYER_JOBS) || !replaying) {
    fireUpdateState(pState, pNode.get());
  } else {
    fireDeactivateState(pState);
  }

  // Reset the number of instructions since forking
  state->_instrSinceFork = 0;
  pState->_instrSinceFork = 0;

}

void JobManager::onStateDestroy(klee::ExecutionState *kState, bool silenced) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  assert(kState);

  if (replaying) {
    CLOUD9_DEBUG("State destroyed during replay");
    kState->getStackTrace().dump(std::cout);
  }

  //CLOUD9_DEBUG("State destroyed");

  SymbolicState *state = kState->getCloud9State();

  if (!silenced) {
    processTestCase(state);
  }

  if (DumpInstrTraces) {
    dumpInstructionTrace(state->getNode().get());
  }

  fireDeactivateState(state);

  updateTreeOnDestroy(kState);
}

void JobManager::onOutOfResources(klee::ExecutionState *destroyedState) {
  // TODO: Implement a job migration mechanism
  CLOUD9_INFO("Executor ran out of resources. Dropping state.");
}

void JobManager::onEvent(klee::ExecutionState *kState,
          unsigned int type, long int value) {
  WorkerTree::Node *node = kState->getCloud9State()->getNode().get();

  switch (type) {
  case KLEE_EVENT_BREAKPOINT:
    if (collectTraces) {
      (**node).trace.appendEntry(new BreakpointEntry(value));
    }

    if (StratOracle) {
      if (value == KLEE_BRK_START_TRACING) {
        kState->getCloud9State()->collectProgress = true; // Enable progress collection in the manager
      }
    }

    break;
  default:
    (**node).trace.appendEntry(new EventEntry(kState->getStackTrace(), type, value));
    break;
  }

}

void JobManager::onControlFlowEvent(klee::ExecutionState *kState,
    ControlFlowEvent event) {
  WorkerTree::Node *node = kState->getCloud9State()->getNode().get();

  // Add the instruction to the node trace
  if (collectTraces) {
    switch (event) {
    case STEP:
      (**node).trace.appendEntry(new InstructionTraceEntry(kState->pc()));
      break;
    case BRANCH_FALSE:
    case BRANCH_TRUE:
      //(**node).trace.appendEntry(new ConstraintLogEntry(state));
      (**node).trace.appendEntry(new ControlFlowEntry(true, false, false));
      break;
    case CALL:
      break;
    case RETURN:
      break;
    }

  }
}

void JobManager::onDebugInfo(klee::ExecutionState *kState,
    const std::string &message) {
  WorkerTree::Node *node = kState->getCloud9State()->getNode().get();

  if (collectTraces) {
    (**node).trace.appendEntry(new DebugLogEntry(message));
  }
}

void JobManager::fireBreakpointHit(WorkerTree::Node *node) {
  SymbolicState *state = (**node).symState;

  CLOUD9_INFO("Breakpoint hit!");
  CLOUD9_DEBUG("State at position: " << *node);

  if (state) {
    CLOUD9_DEBUG("State stack trace: " << *state);
    klee::ExprPPrinter::printConstraints(std::cerr,
        state->getKleeState()->constraints());
    dumpStateTrace(node);
  }

  // Also signal a breakpoint, for stopping GDB
  cloud9::breakSignal();
}

void JobManager::updateTreeOnBranch(klee::ExecutionState *kState,
    klee::ExecutionState *parent, int index, klee::ForkTag forkTag) {
  WorkerTree::NodePin pNodePin = parent->getCloud9State()->getNode();
  (**pNodePin).forkTag = forkTag;

  WorkerTree::Node *newNode, *oldNode;

  // Obtain the new node pointers
  oldNode = tree->getNode(WORKER_LAYER_STATES, pNodePin.get(), 1 - index);
  parent->getCloud9State()->rebindToNode(oldNode);

  newNode = tree->getNode(WORKER_LAYER_STATES, pNodePin.get(), index);
  SymbolicState *state = new SymbolicState(kState);
  state->rebindToNode(newNode);

  if (!replaying) {
    ExecutionJob *job = (**pNodePin).getJob();
    assert(job != NULL);

    oldNode = tree->getNode(WORKER_LAYER_JOBS, oldNode);
    job->rebindToNode(oldNode);

    newNode = tree->getNode(WORKER_LAYER_JOBS, newNode);
    ExecutionJob *newJob = new ExecutionJob(newNode, false);

    submitJob(newJob, false);

    cloud9::instrum::theInstrManager.incStatistic(
                cloud9::instrum::TotalTreePaths);
  }
}

void JobManager::updateTreeOnDestroy(klee::ExecutionState *kState) {
  SymbolicState *state = kState->getCloud9State();

  if (!replaying) {
    WorkerTree::Node *node = state->getNode().get();

    ExecutionJob *job = (**node).getJob();

    assert(job != NULL);
    // Job finished here, need to remove it
    finalizeJob(job, false, false);

    cloud9::instrum::theInstrManager.incStatistic(
        cloud9::instrum::TotalProcJobs);
  }

  state->rebindToNode(NULL);

  kState->setCloud9State(NULL);
  delete state;
}

/* Statistics Management ******************************************************/

void JobManager::refineStatistics() {
  std::set<WorkerTree::NodePin> newStats;

  for (std::set<WorkerTree::NodePin>::iterator it = stats.begin(); it
      != stats.end(); it++) {
    const WorkerTree::NodePin &nodePin = *it;

    if (!nodePin->layerExists(WORKER_LAYER_JOBS)) {
      // Statistic node invalidated
      continue;
    }

    if (nodePin->getCount(WORKER_LAYER_JOBS) > 0) {
      // Move along the path

      WorkerTree::Node *left = nodePin->getChild(WORKER_LAYER_JOBS, 0);
      WorkerTree::Node *right = nodePin->getChild(WORKER_LAYER_JOBS, 1);

      if (left) {
        WorkerTree::NodePin leftPin = tree->getNode(WORKER_LAYER_STATISTICS,
            left)->pin(WORKER_LAYER_STATISTICS);
        newStats.insert(leftPin);
      }

      if (right) {
        WorkerTree::NodePin rightPin = tree->getNode(WORKER_LAYER_STATISTICS,
            right)->pin(WORKER_LAYER_STATISTICS);
        newStats.insert(rightPin);
      }
    } else {
      newStats.insert(nodePin);
    }
  }

  stats = newStats;
  statChanged = true;
}

void JobManager::cleanupStatistics() {

  for (std::set<WorkerTree::NodePin>::iterator it = stats.begin(); it
      != stats.end();) {
    std::set<WorkerTree::NodePin>::iterator oldIt = it++;
    WorkerTree::NodePin nodePin = *oldIt;

    if (!nodePin->layerExists(WORKER_LAYER_JOBS)) {
      stats.erase(oldIt);
      statChanged = true;
    }
  }

  if (stats.empty()) {
    // Add back the root state in the statistics
    WorkerTree::NodePin rootPin = tree->getRoot()->pin(WORKER_LAYER_STATISTICS);
    stats.insert(rootPin);
    statChanged = true;
  }
}

#if 0
void JobManager::getStatisticsData(std::vector<int> &data,
    ExecutionPathSetPin &paths, bool onlyChanged) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  cleanupStatistics();

  if (statChanged || !onlyChanged) {
    std::vector<WorkerTree::Node*> newStats;
    for (std::set<WorkerTree::NodePin>::iterator it = stats.begin(); it
        != stats.end(); it++) {
      newStats.push_back((*it).get());
    }

    paths = tree->buildPathSet(newStats.begin(), newStats.end());
    statChanged = false;

    //CLOUD9_DEBUG("Sent node set: " << getASCIINodeSet(newStats.begin(), newStats.end()));
  }

  data.clear();

  for (std::set<WorkerTree::NodePin>::iterator it = stats.begin(); it
      != stats.end(); it++) {
    const WorkerTree::NodePin &crtNodePin = *it;
    unsigned int jobCount = countJobs(crtNodePin.get());
    data.push_back(jobCount);
  }

  //CLOUD9_DEBUG("Sent data set: " << getASCIIDataSet(data.begin(), data.end()));
}
#else
void JobManager::getStatisticsData(std::vector<int> &data,
    ExecutionPathSetPin &paths, bool onlyChanged) {
  boost::unique_lock<boost::mutex> lock(jobsMutex);

  std::vector<WorkerTree::Node*> dummy;
  dummy.push_back(tree->getRoot());

  paths = tree->buildPathSet(dummy.begin(), dummy.end(),
      (std::map<WorkerTree::Node*,unsigned>*)NULL);

  data.clear();

  data.push_back(jobCount);
}
#endif

/* Coverage Management ********************************************************/

void JobManager::getUpdatedLocalCoverage(cov_update_t &data) {
  theStatisticManager->collectChanges(stats::locallyCoveredInstructions, data);
  theStatisticManager->resetChanges(stats::locallyCoveredInstructions);
}

void JobManager::setUpdatedGlobalCoverage(const cov_update_t &data) {
  for (cov_update_t::const_iterator it = data.begin(); it != data.end(); it++) {
    assert(it->second != 0 && "code uncovered after update");

    uint32_t index = it->first;

    if (!theStatisticManager->getIndexedValue(
        stats::globallyCoveredInstructions, index)) {
      theStatisticManager->incrementIndexedValue(
          stats::globallyCoveredInstructions, index, 1);
      theStatisticManager->incrementIndexedValue(
          stats::globallyUncoveredInstructions, index, (uint64_t) -1);

      assert(!theStatisticManager->getIndexedValue(stats::globallyUncoveredInstructions, index));
    }
  }
}

uint32_t JobManager::getCoverageIDCount() const {
  return symbEngine->getModule()->infos->getMaxID();
}

/* Debugging Support **********************************************************/

void JobManager::setPathBreakpoint(ExecutionPathPin path) {
  assert(0 && "Not yet implemented"); // TODO: Implement this as soon as the
  // tree data structures are refactored
}

void JobManager::setCodeBreakpoint(int assemblyLine) {
  CLOUD9_INFO("Code breakpoint at assembly line " << assemblyLine);
  codeBreaks.insert(assemblyLine);
}

void JobManager::dumpStateTrace(WorkerTree::Node *node) {
  if (!collectTraces) {
    // We have nothing collected, why bother?
    return;
  }

  // Get a file to dump the path into
  char fileName[256];
  snprintf(fileName, 256, "pathDump%05d.txt", traceCounter);
  traceCounter++;

  CLOUD9_INFO("Dumping state trace in file '" << fileName << "'");

  std::ostream *os = kleeHandler->openOutputFile(fileName);
  assert(os != NULL);

  (*os) << (*node) << std::endl;

  SymbolicState *state = (**node).symState;
  assert(state != NULL);

  serializeExecutionTrace(*os, state);

  delete os;
}

void JobManager::dumpInstructionTrace(WorkerTree::Node *node) {
  if (!collectTraces)
    return;

  char fileName[256];
  snprintf(fileName, 256, "instrDump%05d.txt", traceCounter);
  traceCounter++;

  CLOUD9_INFO("Dumping instruction trace in file " << fileName);

  std::ostream *os = kleeHandler->openOutputFile(fileName);
  assert(os != NULL);

  serializeInstructionTrace(*os, node);

  delete os;
}

}
}
