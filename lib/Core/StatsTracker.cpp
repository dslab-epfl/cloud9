//===-- StatsTracker.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StatsTracker.h"

#include "klee/CoreStats.h"
#include "klee/ExecutionState.h"
#include "klee/SolverStats.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"

#include "klee/data/CoverageLogs.pb.h"
#include "klee/data/DebugInfo.pb.h"
#include "klee/data/Profiling.pb.h"
#include "klee/data/Support.h"

#include "CallPathManager.h"
#include "klee/Executor.h"
#include "MemoryManager.h"
#include "UserSearcher.h"

#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/InlineAsm.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Path.h"

#include "cloud9/worker/WorkerCommon.h"

#include <glog/logging.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cxxabi.h>

using namespace klee;
using namespace llvm;

///

namespace {  
  cl::opt<bool>
  TrackInstructionTime("track-instruction-time",
                       cl::desc("Enable tracking of time for individual instructions"),
                       cl::init(false));

  cl::opt<bool>
  OutputStats("output-stats",
              cl::desc("Write running stats trace file"),
              cl::init(true));

  cl::opt<bool>
  OutputIStats("output-istats",
               cl::desc("Write instruction level statistics (in callgrind format)"),
               cl::init(true));

  cl::opt<bool>
  OutputDetailedCoverage("output-detailed-coverage",
      cl::desc("Write periodic detailed coverage information"),
      cl::init(true));

  cl::opt<bool>
  OutputProfileSnapshots("output-profile-snapshots",
      cl::desc("Write periodic callgraph snapshots"),
      cl::init(false));

  cl::opt<bool>
  OutputSolverQueries("output-solver-queries",
      cl::desc("Write the log of solver queries"),
      cl::init(true));

  cl::opt<double>
  StatsWriteInterval("stats-write-interval",
                     cl::desc("Approximate number of seconds between stats writes (default: 1.0)"),
                     cl::init(1.));

  cl::opt<double>
  IStatsWriteInterval("istats-write-interval",
                      cl::desc("Approximate number of seconds between istats writes (default: 10.0)"),
                      cl::init(10.));

  cl::opt<double>
  DetailedLogsInterval("detailed-logs-interval",
      cl::desc("Approx. no. of secs between logging updates (default: 60.0)"),
      cl::init(60.0));

  cl::opt<double>
  LogSummaryInterval("log-summary-interval",
                     cl::desc("Approx. no. of secs between summary logs (default: 5.0)"),
                     cl::init(5.0));

  /*
  cl::opt<double>
  BranchCovCountsWriteInterval("branch-cov-counts-write-interval",
                     cl::desc("Approximate number of seconds between run.branches writes (default: 5.0)"),
                     cl::init(5.));
  */

  // XXX I really would like to have dynamic rate control for something like this.
  cl::opt<double>
  UncoveredUpdateInterval("uncovered-update-interval",
                          cl::init(30.));
  
  cl::opt<bool>
  UseCallPaths("use-call-paths",
               cl::desc("Enable calltree tracking for instruction level statistics"),
               cl::init(true));

  cl::opt<std::string>
  CoverageFocus("coverage-focus",
                cl::desc("The name of a file containing source code to focus on "
                         "when measuring coverage"));
}

///

bool StatsTracker::useStatistics() {
  return OutputStats || OutputIStats;
}

namespace klee {
  class WriteIStatsTimer : public Executor::Timer {
    StatsTracker *statsTracker;
    
  public:
    WriteIStatsTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~WriteIStatsTimer() {}
    
    void run() {
      statsTracker->writeIStats();
    }
  };
  
  class WriteStatsTimer : public Executor::Timer {
    StatsTracker *statsTracker;
    
  public:
    WriteStatsTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~WriteStatsTimer() {}
    
    void run() {
      statsTracker->writeStatsLine();
      statsTracker->writeSolverQueries();
    }
  };

  class DetailedLogsTimer: public Executor::Timer {
    StatsTracker *statsTracker;
  public:
    DetailedLogsTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~DetailedLogsTimer() { }

    void run() {
      statsTracker->commitDetailedLogs();
    }
  };

  class UpdateReachableTimer : public Executor::Timer {
    StatsTracker *statsTracker;
    
  public:
    UpdateReachableTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    
    void run() {
      statsTracker->computeReachableUncovered();
    }
  };

  class LogSummaryTimer : public Executor::Timer {
    StatsTracker *statsTracker;

  public:
    LogSummaryTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~LogSummaryTimer() {}

    void run() {
      statsTracker->logSummary();
    }
  };
 
}

//

/// Check for special cases where we statically know an instruction is
/// uncoverable. Currently the case is an unreachable instruction
/// following a noreturn call; the instruction is really only there to
/// satisfy LLVM's termination requirement.
static bool instructionIsCoverable(Instruction *i) {
  if (i->getOpcode() == Instruction::Unreachable) {
    BasicBlock *bb = i->getParent();
    BasicBlock::iterator it(i);
    if (it==bb->begin()) {
      return true;
    } else {
      Instruction *prev = --it;
      if (isa<CallInst>(prev) || isa<InvokeInst>(prev)) {
        Function *target = getDirectCallTarget(prev);
        if (target && target->doesNotReturn()) {
          return false;
        }
      }
    }
  }

  return true;
}

static std::string CXXDemangle(const std::string &fnName) {
  int status;
  char *realname = abi::__cxa_demangle(fnName.c_str(), NULL, NULL, &status);

  if (status == 0) {
    std::string result(realname);
    free(realname);
    return result;
  } else {
    return fnName;
  }
}

StatsTracker::StatsTracker(Executor &_executor, std::string _objectFilename,
                           bool _updateMinDistToUncovered)
  : executor(_executor),
    objectFilename(_objectFilename),
    statsFile(0),
    istatsFile(0),
    detailedCoverageFile(0),
    profileFile(0),
    queriesFile(0),
    startWallTime(util::getWallTime()),
    numBranches(0),
    fullBranches(0),
    partialBranches(0) {
  KModule *km = executor.kmodule;

  sys::Path module(objectFilename);
  if (!llvm::sys::path::is_absolute(Twine(objectFilename))) {
    sys::Path current = sys::Path::GetCurrentDirectory();
    current.appendComponent(objectFilename);
    if (current.isValid())
      objectFilename = current.c_str();
  }

  if (OutputIStats)
    theStatisticManager->useIndexedStats(km->infos->getMaxID());

  for (std::vector<KFunction*>::iterator it = km->functions.begin(), 
         ie = km->functions.end(); it != ie; ++it) {
    KFunction *kf = *it;
    //kf->trackCoverage = 1;

    for (unsigned i=0; i<kf->numInstructions; ++i) {
      KInstruction *ki = kf->instructions[i];

      if (OutputIStats) {
        unsigned id = ki->info->id;
        theStatisticManager->setIndex(id);
        if (instructionIsCoverable(ki->inst)) {
          ++stats::locallyUncoveredInstructions;
          ++stats::globallyUncoveredInstructions;
        }
      }

      if (BranchInst *bi = dyn_cast<BranchInst>(ki->inst))
        if (!bi->isUnconditional())
          numBranches++;

    }
  }

  if (OutputStats) {
    statsFile = executor.interpreterHandler->openOutputFile("run.stats");
    assert(statsFile && "unable to open statistics trace file");
    writeStatsHeader();
    writeStatsLine();

    executor.addTimer(new WriteStatsTimer(this), StatsWriteInterval);

    pstatsFile = executor.interpreterHandler->openOutputFile("stats.bin");
    assert(pstatsFile && "unable to open statistics protobuf file");
  }

  computeReachableUncovered();
  executor.addTimer(new UpdateReachableTimer(this), UncoveredUpdateInterval);

  if (OutputIStats) {
    istatsFile = executor.interpreterHandler->openOutputFile("run.istats");
    assert(istatsFile && "unable to open istats file");

    executor.addTimer(new WriteIStatsTimer(this), IStatsWriteInterval);
  }

  if (CoverageFocus != "") {
    std::ifstream ifs(CoverageFocus.getValue().c_str());
    assert(!ifs.fail() && "Could not open the coverage focus file");
    readCoverageFocus(ifs);
  }

  if (OutputDetailedCoverage) {
    detailedCoverageFile = executor.interpreterHandler->openOutputFile("coverage.bin");
    assert(detailedCoverageFile && "unable to open detailed coverage file");
    writeDetailedCoverageSnapshot(true);
  }

  if (OutputProfileSnapshots) {
    profileFile = executor.interpreterHandler->openOutputFile("profile.bin");
    assert(profileFile && "unable to open callgraph file");
  }

  if (OutputSolverQueries) {
    queriesFile = executor.interpreterHandler->openOutputFile("queries.bin");
    assert(queriesFile && "unable to open constraints file");
  }

  statesFile = executor.interpreterHandler->openOutputFile("states.bin");
  assert(statesFile && "unable to open states file");

  executor.addTimer(new DetailedLogsTimer(this), DetailedLogsInterval);
  executor.addTimer(new LogSummaryTimer(this), LogSummaryInterval);
}

StatsTracker::~StatsTracker() {  
  if (statsFile)
    delete statsFile;
  if (istatsFile)
    delete istatsFile;
  if (pstatsFile)
    delete pstatsFile;
  // TODO(bucur): Fix the rest of the leaks
}

void StatsTracker::done() {
  if (statsFile)
    writeStatsLine();
  if (OutputIStats)
    writeIStats();
  writeSolverQueries();
  writeStates();
  writeStatistics();
}

void StatsTracker::stepInstruction(ExecutionState &es) {
  if (OutputIStats) {
    if (TrackInstructionTime) {
      static sys::TimeValue lastNowTime(0, 0), lastUserTime(0, 0);

      if (lastUserTime.seconds() == 0 && lastUserTime.nanoseconds() == 0) {
        sys::TimeValue sys(0, 0);
        sys::Process::GetTimeUsage(lastNowTime, lastUserTime, sys);
      } else {
        sys::TimeValue now(0, 0), user(0, 0), sys(0, 0);
        sys::Process::GetTimeUsage(now, user, sys);
        sys::TimeValue delta = user - lastUserTime;
        sys::TimeValue deltaNow = now - lastNowTime;
        stats::instructionTime += delta.usec();
        stats::instructionRealTime += deltaNow.usec();
        lastUserTime = user;
        lastNowTime = now;
      }
    }

    Instruction *inst = es.pc()->inst;
    const InstructionInfo &ii = *es.pc()->info;
    StackFrame &sf = es.stack().back();
    theStatisticManager->setIndex(ii.id);
    if (UseCallPaths)
      theStatisticManager->setContext(&sf.callPathNode->statistics);

    if (es.instsSinceCovNew)
      ++es.instsSinceCovNew;

    if (instructionIsCoverable(inst)) {
      if (!theStatisticManager->getIndexedValue(
          stats::locallyCoveredInstructions, ii.id)) {
        // Checking for actual stoppoints avoids inconsistencies due
        // to line number propogation.
        es.coveredLines[&ii.file].insert(ii.line);
        es.setCoveredNew();
        es.instsSinceCovNew = 1;
        ++stats::locallyCoveredInstructions;
        stats::locallyUncoveredInstructions += (uint64_t) -1;

        if (!theStatisticManager->getIndexedValue(stats::globallyCoveredInstructions, ii.id)) {
          ++stats::globallyCoveredInstructions;
          stats::globallyUncoveredInstructions += (uint64_t) -1;
        }
      }
    }
  }
}

void StatsTracker::framePushed(StackFrame *frame, StackFrame *parentFrame) {
  if (OutputIStats) {

    if (UseCallPaths) {
      CallPathNode *parent = parentFrame ? parentFrame->callPathNode : 0;
      CallPathNode *cp = callPathManager.getCallPath(parent,
                                                     frame->caller ? frame->caller->inst : 0,
                                                     frame->kf->function);
      frame->callPathNode = cp;
      cp->count++;
    }

    uint64_t minDistAtRA = 0;
    if (parentFrame)
      minDistAtRA = parentFrame->minDistToUncoveredOnReturn;

    frame->minDistToUncoveredOnReturn = frame->caller ?
      computeMinDistToUncovered(frame->caller, minDistAtRA) : 0;
  }
}

/* Should be called _after_ the es->pushFrame() */
void StatsTracker::framePushed(ExecutionState &es, StackFrame *parentFrame) {
  framePushed(&es.stack().back(), parentFrame);
}

/* Should be called _after_ the es->popFrame() */
void StatsTracker::framePopped(ExecutionState &es) {
  // XXX remove me?
}


void StatsTracker::markBranchVisited(ExecutionState *visitedTrue, 
                                     ExecutionState *visitedFalse) {
  if (OutputIStats) {
    unsigned id = theStatisticManager->getIndex();
    uint64_t hasTrue = theStatisticManager->getIndexedValue(stats::trueBranches, id);
    uint64_t hasFalse = theStatisticManager->getIndexedValue(stats::falseBranches, id);
    if (visitedTrue && !hasTrue) {
      visitedTrue->setCoveredNew();
      visitedTrue->instsSinceCovNew = 1;
      ++stats::trueBranches;
      if (hasFalse) { ++fullBranches; --partialBranches; }
      else ++partialBranches;
      hasTrue = 1;
    }
    if (visitedFalse && !hasFalse) {
      visitedFalse->setCoveredNew();
      visitedFalse->instsSinceCovNew = 1;
      ++stats::falseBranches;
      if (hasTrue) { ++fullBranches; --partialBranches; }
      else ++partialBranches;
    }
  }
}

void StatsTracker::writeStatsHeader() {
  *statsFile << "('Instructions',"
             << "'FullBranches',"
             << "'PartialBranches',"
             << "'NumBranches',"
             << "'UserTime',"
             << "'NumStates',"
             << "'MallocUsage',"
             << "'NumQueries',"
             << "'NumQueryConstructs',"
             << "'NumObjects',"
             << "'WallTime',"
             << "'CoveredInstructions',"
             << "'UncoveredInstructions',"
             << "'QueryTime',"
             << "'SolverTime',"
             << "'CexCacheTime',"
             << "'ForkTime',"
             << "'ResolveTime',"
             << ")\n";
  statsFile->flush();
}

double StatsTracker::elapsed() {
  return util::getWallTime() - startWallTime;
}

void StatsTracker::writeStatsLine() {
  *statsFile << "(" << stats::instructions
             << "," << fullBranches
             << "," << partialBranches
             << "," << numBranches
             << "," << util::getUserTime()
             << "," << executor.states.size()
             << "," << sys::Process::GetTotalMemoryUsage()
             << "," << stats::queries
             << "," << stats::queryConstructs
             << "," << 0 // was numObjects
             << "," << elapsed()
             << "," << stats::locallyCoveredInstructions
             << "," << stats::locallyUncoveredInstructions
             << "," << stats::queryTime / 1000000.
             << "," << stats::solverTime / 1000000.
             << "," << stats::cexCacheTime / 1000000.
             << "," << stats::forkTime / 1000000.
             << "," << stats::resolveTime / 1000000.
             << ")\n";
  statsFile->flush();
}

void StatsTracker::recordStats() {
  data::Statistic *statistic = currentStatisticsSet.add_statistic();

  statistic->set_time_stamp(sys::TimeValue::now().toPosixTime());

  statistic->set_instructions(stats::instructions);
  statistic->set_full_branches(fullBranches);
  statistic->set_partial_branches(partialBranches);
  statistic->set_user_time(util::getUserTime());
  statistic->set_num_states(executor.states.size());
  statistic->set_num_queries(stats::queries);
  statistic->set_num_query_constructs(stats::queryConstructs);
  statistic->set_wall_time(elapsed());
  statistic->set_covered_instructions(stats::locallyCoveredInstructions);
  statistic->set_uncovered_instructions(stats::locallyUncoveredInstructions);
}

void StatsTracker::updateStateStatistics(uint64_t addend) {
  for (std::set<ExecutionState*>::iterator it = executor.states.begin(),
         ie = executor.states.end(); it != ie; ++it) {
    ExecutionState &state = **it;
    const InstructionInfo &ii = *state.pc()->info;
    theStatisticManager->incrementIndexedValue(stats::states, ii.id, addend);
    if (UseCallPaths)
      state.stack().back().callPathNode->statistics.incrementValue(stats::states, addend);
  }
}

void StatsTracker::writeIStats() {
  Module *m = executor.kmodule->module;
  uint64_t istatsMask = 0;
  std::ostream &of = *istatsFile;
  
  of.seekp(0, std::ios::end);
  unsigned istatsSize = of.tellp();
  of.seekp(0);

  of << "version: 1\n";
  of << "creator: klee\n";
  of << "pid: " << sys::Process::GetCurrentUserId() << "\n";
  of << "cmd: " << m->getModuleIdentifier() << "\n\n";
  of << "\n";
  
  StatisticManager &sm = *theStatisticManager;
  unsigned nStats = sm.getNumStatistics();

  // Max is 13, sadly
  istatsMask |= 1<<sm.getStatisticID("Queries");
  istatsMask |= 1<<sm.getStatisticID("QueriesValid");
  istatsMask |= 1<<sm.getStatisticID("QueriesInvalid");
  istatsMask |= 1<<sm.getStatisticID("QueryTime");
  istatsMask |= 1<<sm.getStatisticID("ResolveTime");
  istatsMask |= 1<<sm.getStatisticID("Instructions");
  istatsMask |= 1<<sm.getStatisticID("InstructionTimes");
  istatsMask |= 1<<sm.getStatisticID("InstructionRealTimes");
  istatsMask |= 1<<sm.getStatisticID("Forks");
  istatsMask |= 1<<sm.getStatisticID("CoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("UncoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("States");
  istatsMask |= 1<<sm.getStatisticID("MinDistToUncovered");

  of << "positions: instr line\n";

  for (unsigned i=0; i<nStats; i++) {
    if (istatsMask & (1<<i)) {
      Statistic &s = sm.getStatistic(i);
      of << "event: " << s.getShortName() << " : " 
         << s.getName() << "\n";
    }
  }

  of << "events: ";
  for (unsigned i=0; i<nStats; i++) {
    if (istatsMask & (1<<i))
      of << sm.getStatistic(i).getShortName() << " ";
  }
  of << "\n";
  
  // set state counts, decremented after we process so that we don't
  // have to zero all records each time.
  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics(1);

  std::string sourceFile = "";

  CallSiteSummaryTable callSiteStats;
  if (UseCallPaths)
    callPathManager.getSummaryStatistics(callSiteStats);

  of << "ob=" << objectFilename << "\n";

  for (Module::iterator fnIt = m->begin(), fn_ie = m->end(); 
       fnIt != fn_ie; ++fnIt) {
    if (!fnIt->isDeclaration()) {
      of << "fn=" << CXXDemangle(fnIt->getName().str()) << "\n";
      for (Function::iterator bbIt = fnIt->begin(), bb_ie = fnIt->end(); 
           bbIt != bb_ie; ++bbIt) {
        for (BasicBlock::iterator it = bbIt->begin(), ie = bbIt->end(); 
             it != ie; ++it) {
          Instruction *instr = &*it;
          const InstructionInfo &ii = executor.kmodule->infos->getInfo(instr);
          unsigned index = ii.id;
          if (ii.file!=sourceFile) {
            of << "fl=" << ii.file << "\n";
            sourceFile = ii.file;
          }
          of << ii.assemblyLine << " ";
          of << ii.line << " ";
          for (unsigned i=0; i<nStats; i++)
            if (istatsMask&(1<<i))
              of << sm.getIndexedValue(sm.getStatistic(i), index) << " ";
          of << "\n";

          if (UseCallPaths && 
              (isa<CallInst>(instr) || isa<InvokeInst>(instr))) {
            CallSiteSummaryTable::iterator it = callSiteStats.find(instr);
            if (it!=callSiteStats.end()) {
              for (std::map<llvm::Function*, CallSiteInfo>::iterator
                     fit = it->second.begin(), fie = it->second.end(); 
                   fit != fie; ++fit) {
                Function *f = fit->first;
                CallSiteInfo &csi = fit->second;
                const InstructionInfo &fii = 
                  executor.kmodule->infos->getFunctionInfo(f);
  
                if (fii.file!="" && fii.file!=sourceFile)
                  of << "cfl=" << fii.file << "\n";
                of << "cfn=" << CXXDemangle(f->getName().str()) << "\n";
                of << "calls=" << csi.count << " ";
                of << fii.assemblyLine << " ";
                of << fii.line << "\n";

                of << ii.assemblyLine << " ";
                of << ii.line << " ";
                for (unsigned i=0; i<nStats; i++) {
                  if (istatsMask&(1<<i)) {
                    Statistic &s = sm.getStatistic(i);
                    uint64_t value;

                    // Hack, ignore things that don't make sense on
                    // call paths.
                    if (&s == &stats::locallyUncoveredInstructions) {
                      value = 0;
                    } else {
                      value = csi.statistics.getValue(s);
                    }

                    of << value << " ";
                  }
                }
                of << "\n";
              }
            }
          }
        }
      }
    }
  }

  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics((uint64_t)-1);
  
  // Clear then end of the file if necessary (no truncate op?).
  unsigned pos = of.tellp();
  for (unsigned i=pos; i<istatsSize; ++i)
    of << '\n';
  
  of.flush();
}

///

typedef std::map<Instruction*, std::vector<Function*> > calltargets_ty;

static calltargets_ty callTargets;
static std::map<Function*, std::vector<Instruction*> > functionCallers;
static std::map<Function*, unsigned> functionShortestPath;

static std::vector<Instruction*> getSuccs(Instruction *i) {
  BasicBlock *bb = i->getParent();
  std::vector<Instruction*> res;

  if (i==bb->getTerminator()) {
    for (succ_iterator it = succ_begin(bb), ie = succ_end(bb); it != ie; ++it)
      res.push_back(it->begin());
  } else {
    res.push_back(++BasicBlock::iterator(i));
  }

  return res;
}

uint64_t klee::computeMinDistToUncovered(const KInstruction *ki,
                                         uint64_t minDistAtRA) {
  StatisticManager &sm = *theStatisticManager;
  if (minDistAtRA==0) { // unreachable on return, best is local
    return sm.getIndexedValue(stats::minDistToGloballyUncovered,
                              ki->info->id);
  } else {
    uint64_t minDistLocal = sm.getIndexedValue(stats::minDistToGloballyUncovered,
                                               ki->info->id);
    uint64_t distToReturn = sm.getIndexedValue(stats::minDistToReturn,
                                               ki->info->id);

    if (distToReturn==0) { // return unreachable, best is local
      return minDistLocal;
    } else if (!minDistLocal) { // no local reachable
      return distToReturn + minDistAtRA;
    } else {
      return std::min(minDistLocal, distToReturn + minDistAtRA);
    }
  }
}

void StatsTracker::computeReachableUncovered() {
  KModule *km = executor.kmodule;
  Module *m = km->module;
  static bool init = true;
  const InstructionInfoTable &infos = *km->infos;
  StatisticManager &sm = *theStatisticManager;
  
  if (init) {
    init = false;

    // Compute call targets. It would be nice to use alias information
    // instead of assuming all indirect calls hit all escaping
    // functions, eh?
    for (Module::iterator fnIt = m->begin(), fn_ie = m->end(); 
         fnIt != fn_ie; ++fnIt) {
      for (Function::iterator bbIt = fnIt->begin(), bb_ie = fnIt->end(); 
           bbIt != bb_ie; ++bbIt) {
        for (BasicBlock::iterator it = bbIt->begin(), ie = bbIt->end(); 
             it != ie; ++it) {
          if (isa<CallInst>(it) || isa<InvokeInst>(it)) {
            if (isa<InlineAsm>(it->getOperand(0))) {
              // We can never call through here so assume no targets
              // (which should be correct anyhow).
              callTargets.insert(std::make_pair(it,
                                                std::vector<Function*>()));
            } else if (Function *target = getDirectCallTarget(it)) {
              callTargets[it].push_back(target);
            } else {
              callTargets[it] = 
                std::vector<Function*>(km->escapingFunctions.begin(),
                                       km->escapingFunctions.end());
            }
          }
        }
      }
    }

    // Compute function callers as reflexion of callTargets.
    for (calltargets_ty::iterator it = callTargets.begin(), 
           ie = callTargets.end(); it != ie; ++it)
      for (std::vector<Function*>::iterator fit = it->second.begin(), 
             fie = it->second.end(); fit != fie; ++fit) 
        functionCallers[*fit].push_back(it->first);

    // Initialize minDistToReturn to shortest paths through
    // functions. 0 is unreachable.
    std::vector<Instruction *> instructions;
    for (Module::iterator fnIt = m->begin(), fn_ie = m->end(); 
         fnIt != fn_ie; ++fnIt) {
      if (fnIt->isDeclaration()) {
        if (fnIt->doesNotReturn()) {
          functionShortestPath[fnIt] = 0;
        } else {
          functionShortestPath[fnIt] = 1; // whatever
        }
        continue;
      } else {
        functionShortestPath[fnIt] = 0;
      }

      KFunction *kf = km->functionMap[fnIt];

      for (unsigned i = 0; i < kf->numInstructions; ++i) {
        Instruction *inst = kf->instrPostOrder[i]->inst;
        instructions.push_back(inst);
        sm.setIndexedValue(stats::minDistToReturn,
                           kf->instrPostOrder[i]->info->id,
                           isa<ReturnInst>(inst));
      }
    }
    
    // I'm so lazy it's not even worklisted.
    bool changed;
    do {
      changed = false;
      for (std::vector<Instruction*>::iterator it = instructions.begin(),
             ie = instructions.end(); it != ie; ++it) {
        Instruction *inst = *it;
        unsigned bestThrough = 0;

        if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
          std::vector<Function*> &targets = callTargets[inst];
          for (std::vector<Function*>::iterator fnIt = targets.begin(),
                 ie = targets.end(); fnIt != ie; ++fnIt) {
            uint64_t dist = functionShortestPath[*fnIt];
            if (dist) {
              dist = 1+dist; // count instruction itself
              if (bestThrough==0 || dist<bestThrough)
                bestThrough = dist;
            }
          }
        } else {
          bestThrough = 1;
        }
       
        if (bestThrough) {
          unsigned id = infos.getInfo(*it).id;
          uint64_t best, cur = best = sm.getIndexedValue(stats::minDistToReturn, id);
          std::vector<Instruction*> succs = getSuccs(*it);
          for (std::vector<Instruction*>::iterator it2 = succs.begin(),
                 ie = succs.end(); it2 != ie; ++it2) {
            uint64_t dist = sm.getIndexedValue(stats::minDistToReturn,
                                               infos.getInfo(*it2).id);
            if (dist) {
              uint64_t val = bestThrough + dist;
              if (best==0 || val<best)
                best = val;
            }
          }
          if (best != cur) {
            sm.setIndexedValue(stats::minDistToReturn, id, best);
            changed = true;

            // Update shortest path if this is the entry point.
            Function *f = inst->getParent()->getParent();
            if (inst==f->begin()->begin())
              functionShortestPath[f] = best;
          }
        }
      }
    } while (changed);
  }

  // compute minDistToUncovered, 0 is unreachable
  std::vector<Instruction *> instructions;
  std::vector<unsigned> ids;

  for (Module::iterator fnIt = m->begin(), fn_ie = m->end(); 
       fnIt != fn_ie; ++fnIt) {
    if (fnIt->isDeclaration())
      continue;

    KFunction *kf = km->functionMap[fnIt];

    for (unsigned i = 0; i < kf->numInstructions; ++i) {
      Instruction *inst = kf->instrPostOrder[i]->inst;
      unsigned id = kf->instrPostOrder[i]->info->id;
      instructions.push_back(inst);
      ids.push_back(id);
      sm.setIndexedValue(stats::minDistToGloballyUncovered,
                         id,
                         sm.getIndexedValue(stats::globallyUncoveredInstructions, id));
    }
  }
  
  // I'm so lazy it's not even worklisted.
  bool changed;
  do {
    changed = false;
    for (unsigned i = 0; i < instructions.size(); ++i) {
      Instruction *inst = instructions[i];
      unsigned id = ids[i];

      uint64_t best, cur = best = sm.getIndexedValue(stats::minDistToGloballyUncovered, 
                                                     id);
      unsigned bestThrough = 0;
      
      if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
        std::vector<Function*> &targets = callTargets[inst];
        for (std::vector<Function*>::iterator fnIt = targets.begin(),
               ie = targets.end(); fnIt != ie; ++fnIt) {
          uint64_t dist = functionShortestPath[*fnIt];
          if (dist) {
            dist = 1+dist; // count instruction itself
            if (bestThrough==0 || dist<bestThrough)
              bestThrough = dist;
          }

          if (!(*fnIt)->isDeclaration()) {
            uint64_t calleeDist = sm.getIndexedValue(stats::minDistToGloballyUncovered,
                                                     infos.getFunctionInfo(*fnIt).id);
            if (calleeDist) {
              calleeDist = 1+calleeDist; // count instruction itself
              if (best==0 || calleeDist<best)
                best = calleeDist;
            }
          }
        }
      } else {
        bestThrough = 1;
      }
      
      if (bestThrough) {
        std::vector<Instruction*> succs = getSuccs(inst);
        for (std::vector<Instruction*>::iterator it2 = succs.begin(),
               ie = succs.end(); it2 != ie; ++it2) {
          uint64_t dist = sm.getIndexedValue(stats::minDistToGloballyUncovered,
                                             infos.getInfo(*it2).id);
          if (dist) {
            uint64_t val = bestThrough + dist;
            if (best==0 || val<best)
              best = val;
          }
        }
      }

      if (best != cur) {
        sm.setIndexedValue(stats::minDistToGloballyUncovered, 
                           infos.getInfo(inst).id, 
                           best);
        changed = true;
      }
    }
  } while (changed);

  for (std::set<ExecutionState*>::iterator it = executor.states.begin(),
         ie = executor.states.end(); it != ie; ++it) {
    ExecutionState *es = *it;
    uint64_t currentFrameMinDist = 0;
    for (ExecutionState::stack_ty::iterator sfIt = es->stack().begin(),
           sf_ie = es->stack().end(); sfIt != sf_ie; ++sfIt) {
      ExecutionState::stack_ty::iterator next = sfIt + 1;
      KInstIterator kii;

      if (next==es->stack().end()) {
        kii = es->pc();
      } else {
        kii = next->caller;
        ++kii;
      }
      
      sfIt->minDistToUncoveredOnReturn = currentFrameMinDist;
      
      currentFrameMinDist = computeMinDistToUncovered(kii, currentFrameMinDist);
    }
  }

  LOG(INFO) << "Processed " << instructions.size() << " instructions in static analysis";
}


void StatsTracker::computeDetailedCoverageSnapshot(data::CoverageInfo &coverageInfo,
    bool complete) {
  KModule *km = executor.kmodule;

  typedef std::map<int, std::set<int> > src_coverage_t;

  src_coverage_t global_source_coverage;

  if (complete) {
    lastGlobalCoverage.total_asm_lines = 0;
    lastGlobalCoverage.total_source_lines = 0;
    lastFocusedCoverage.total_asm_lines = 0;
    lastFocusedCoverage.total_source_lines = 0;
  } else {
    lastGlobalCoverage.covered_asm_lines = 0;
    lastGlobalCoverage.covered_source_lines = 0;
    lastFocusedCoverage.covered_asm_lines = 0;
    lastFocusedCoverage.covered_source_lines = 0;
  }

  for (std::vector<KFunction*>::iterator fit = km->functions.begin(),
      fie = km->functions.end(); fit != fie; fit++) {
    KFunction* kf = *fit;

    src_coverage_t source_coverage;
    data::FunctionCoverageInfo *fnCovInfo = coverageInfo.add_function_coverage();
    fnCovInfo->set_function_id(kf->nameID);

    for (unsigned i = 0; i < kf->numInstructions; i++) {
      if (!instructionIsCoverable(kf->instructions[i]->inst))
        continue;
      unsigned id = kf->instructions[i]->info->id;
      if (!complete && !theStatisticManager->getIndexedValue(
          stats::globallyCoveredInstructions, id))
        continue;

      int file_id = kf->instructions[i]->info->file_id;
      unsigned line = kf->instructions[i]->info->line;
      unsigned asm_line = kf->instructions[i]->info->assemblyLine;

      source_coverage[file_id].insert(line);
      global_source_coverage[file_id].insert(line);
      fnCovInfo->add_covered_asm_line(asm_line);

      if (complete) {
        lastGlobalCoverage.total_asm_lines++;
        if (coverageFocus.count(file_id) > 0)
          lastFocusedCoverage.total_asm_lines++;
      } else {
        lastGlobalCoverage.covered_asm_lines++;
        if (coverageFocus.count(file_id) > 0)
          lastFocusedCoverage.covered_asm_lines++;
      }
    }

    for (src_coverage_t::iterator cit = source_coverage.begin(),
        cie = source_coverage.end(); cit != cie; ++cit) {
      data::SourceFileCoverageInfo *srcCovInfo =
          fnCovInfo->add_source_coverage();
      srcCovInfo->set_file_id(cit->first);
      for (std::set<int>::iterator lit = cit->second.begin(),
          lie = cit->second.end(); lit != lie; ++lit) {
        srcCovInfo->add_covered_line(*lit);
      }
    }
  }

  for (src_coverage_t::iterator cit = global_source_coverage.begin(),
      cie = global_source_coverage.end(); cit != cie; ++cit) {
    if (complete) {
      lastGlobalCoverage.total_source_lines += cit->second.size();
      if (coverageFocus.count(cit->first) > 0)
        lastFocusedCoverage.total_source_lines += cit->second.size();
    } else {
      lastGlobalCoverage.covered_source_lines += cit->second.size();
      if (coverageFocus.count(cit->first) > 0)
        lastFocusedCoverage.covered_source_lines += cit->second.size();
    }
  }

  coverageInfo.set_time_stamp(::time(NULL));
  coverageInfo.set_total_source_lines(lastGlobalCoverage.total_source_lines);
  coverageInfo.set_total_asm_lines(lastGlobalCoverage.total_asm_lines);
  coverageInfo.set_covered_source_lines(lastGlobalCoverage.covered_source_lines);
  coverageInfo.set_covered_asm_lines(lastGlobalCoverage.covered_asm_lines);
}

void StatsTracker::writeDetailedCoverageSnapshot(bool is_header) {
  data::CoverageInfo coverageInfo;
  computeDetailedCoverageSnapshot(coverageInfo, is_header);
  WriteProtoMessage(coverageInfo, *detailedCoverageFile, true);
}

void StatsTracker::writeCallgraphProfile() {
  data::GlobalProfile globalProfile;
  getCallgraphProfile(globalProfile);
  WriteProtoMessage(globalProfile, *profileFile, true);
}

void StatsTracker::writeSolverQueries() {
  if (!currentQuerySet.solver_query_size())
    return;

  WriteProtoMessage(currentQuerySet, *queriesFile, true);
  currentQuerySet.Clear();
}

void StatsTracker::writeStatistics() {
  if (!currentStatisticsSet.statistic_size())
    return;

  WriteProtoMessage(currentStatisticsSet, *statsFile, true);
  currentStatisticsSet.Clear();
}

void StatsTracker::writeStates() {
  flushAllStates();

  if (!currentStatesSet.execution_state_size())
    return;

  WriteProtoMessage(currentStatesSet, *statesFile, true);
  currentStatesSet.Clear();
}

// TODO(sbucur): Break this into multiple methods
void StatsTracker::getCallgraphProfile(data::GlobalProfile &globalProfile) {
  Module *m = executor.kmodule->module;
  uint64_t istatsMask = 0;

  StatisticManager &sm = *theStatisticManager;
  unsigned nStats = sm.getNumStatistics();

  istatsMask |= 1<<sm.getStatisticID("Queries");
  istatsMask |= 1<<sm.getStatisticID("QueriesValid");
  istatsMask |= 1<<sm.getStatisticID("QueriesInvalid");
  istatsMask |= 1<<sm.getStatisticID("QueryTime");
  istatsMask |= 1<<sm.getStatisticID("ResolveTime");
  istatsMask |= 1<<sm.getStatisticID("Instructions");
  istatsMask |= 1<<sm.getStatisticID("InstructionTimes");
  istatsMask |= 1<<sm.getStatisticID("InstructionRealTimes");
  istatsMask |= 1<<sm.getStatisticID("Forks");
  istatsMask |= 1<<sm.getStatisticID("GloballyCoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("GloballyUncoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("States");
  istatsMask |= 1<<sm.getStatisticID("MinDistToUncovered");

  for (unsigned i=0; i<nStats; i++) {
    if (istatsMask & (1<<i)) {
      Statistic &s = sm.getStatistic(i);
      globalProfile.add_cost_label(s.getName());
    }
  }

  globalProfile.set_time_stamp(::time(NULL));

  // set state counts, decremented after we process so that we don't
  // have to zero all records each time.
  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics(1);

  CallSiteSummaryTable callSiteStats;
  if (UseCallPaths)
    callPathManager.getSummaryStatistics(callSiteStats);

  for (Module::iterator fnIt = m->begin(), fn_ie = m->end();
       fnIt != fn_ie; ++fnIt) {
    if (fnIt->isDeclaration())
      continue;

    data::FunctionProfile *functionProfile = globalProfile.add_function_profile();
    functionProfile->set_function_id(executor.kmodule->functionMap[&(*fnIt)]->nameID);

    for (Function::iterator bbIt = fnIt->begin(), bb_ie = fnIt->end();
         bbIt != bb_ie; ++bbIt) {
      for (BasicBlock::iterator it = bbIt->begin(), ie = bbIt->end();
           it != ie; ++it) {
        Instruction *instr = &*it;
        const InstructionInfo &ii = executor.kmodule->infos->getInfo(instr);
        unsigned index = ii.id;

        data::LineProfile *lineProfile = functionProfile->add_line_profile();
        executor.kmodule->fillInstructionDebugInfo(
            instr, *lineProfile->mutable_debug_info());

        for (unsigned i=0; i<nStats; i++) {
          if (istatsMask&(1<<i)) {
            lineProfile->add_cost_value(
                sm.getIndexedValue(sm.getStatistic(i), index));
          }
        }

        if (UseCallPaths &&
            (isa<CallInst>(instr) || isa<InvokeInst>(instr))) {
          CallSiteSummaryTable::iterator it = callSiteStats.find(instr);
          if (it!=callSiteStats.end()) {
            for (std::map<llvm::Function*, CallSiteInfo>::iterator
                   fit = it->second.begin(), fie = it->second.end();
                 fit != fie; ++fit) {
              Function *f = fit->first;
              CallSiteInfo &csi = fit->second;

              data::CallSiteProfile *callsiteProfile = lineProfile->add_call_site_profile();
              executor.kmodule->fillFunctionDebugInfo(
                  f, *callsiteProfile->mutable_debug_info());

              callsiteProfile->set_call_count(csi.count);

              for (unsigned i=0; i<nStats; i++) {
                if (istatsMask&(1<<i)) {
                  Statistic &s = sm.getStatistic(i);
                  uint64_t value;

                  // Hack, ignore things that don't make sense on
                  // call paths.
                  if (&s == &stats::globallyUncoveredInstructions) {
                    value = 0;
                  } else {
                    value = csi.statistics.getValue(s);
                  }

                  callsiteProfile->add_cost_value(value);
                }
              }
            }
          }
        }
      }
    }
  }

  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics((uint64_t)-1);
}

void StatsTracker::commitDetailedLogs() {
  if (OutputDetailedCoverage) {
    writeDetailedCoverageSnapshot(false);
  }
  if (OutputProfileSnapshots) {
    writeCallgraphProfile();
  }
  writeStates();
}

void StatsTracker::readCoverageFocus(std::istream &is) {
  KModule *km = executor.kmodule;

  while (!is.eof()) {
    std::string file_name;
    is >> file_name;

    if (is.eof() && file_name.length() == 0)
      break;

    for (StringTable::iterator it = km->infos->stringTable.begin(),
        ie = km->infos->stringTable.end(); it != ie; ++it) {
      const std::string *str = it->first;
      if (str->rfind(file_name) != std::string::npos)
        coverageFocus.insert(it->second);
    }
  }
}

std::string StatsTracker::formatCoverageInfo(unsigned total, unsigned covered) {
  std::stringstream oss;

  if (total) {
    oss << (100L * covered / total);
    oss << "%";
  } else {
    oss << "-";
  }
  oss << " (" << covered << "/" << total << ")";

  return oss.str();
}

void StatsTracker::logSummary() {
  LOG(INFO) << "[STATS] StateCount: " << executor.states.size()
      << " InstructionCount: " << stats::instructions
      << " QueryCount: " << stats::queries;

  LOG(INFO) << "[GLOBAL COVERAGE] "
      << "Source: " << formatCoverageInfo(lastGlobalCoverage.total_source_lines,
                                          lastGlobalCoverage.covered_source_lines)
      << " Instructions: " << formatCoverageInfo(lastGlobalCoverage.total_asm_lines,
                                                 lastGlobalCoverage.covered_asm_lines);
  LOG(INFO) << "[FOCUSED COVERAGE] "
      << "Source: " << formatCoverageInfo(lastFocusedCoverage.total_source_lines,
                                          lastFocusedCoverage.covered_source_lines)
      << " Instructions: " << formatCoverageInfo(lastFocusedCoverage.total_asm_lines,
                                                 lastFocusedCoverage.covered_asm_lines);
}

void StatsTracker::recordSolverQuery(uint64_t time_stamp, uint64_t solving_time,
    data::QueryReason reason, data::QueryOperation operation,
    bool shadow, const ExecutionState &state) {
  data::SolverQuery *query = currentQuerySet.add_solver_query();

  query->set_time_stamp(time_stamp);
  query->set_solving_time(solving_time);
  query->set_reason(reason);
  query->set_operation(operation);

  KInstruction *ki = state.prevPC();
  executor.kmodule->fillInstructionDebugInfo(
      ki->inst, *query->mutable_debug_info());

  query->set_shadow(shadow);

  data::ExecutionState *es_data = query->mutable_execution_state();
  recordStateUpdate(state, false, false, es_data);
}

void StatsTracker::updateStates(ExecutionState *current,
                                const std::set<ExecutionState*> &addedStates,
                                const std::set<ExecutionState*> &removedStates) {
  if (!addedStates.empty()) {
    for (std::set<ExecutionState*>::iterator it = addedStates.begin(),
        ie = addedStates.end(); it != ie; ++it) {
      currentStates.insert(*it);
    }
  }

  if (!removedStates.empty()) {
    for (std::set<ExecutionState*>::iterator it = removedStates.begin(),
        ie = removedStates.end(); it != ie; ++it) {
      currentStates.erase(*it);
      data::ExecutionState *es_data = currentStatesSet.add_execution_state();
      recordStateUpdate(*(*it), true, true, es_data);
    }
  }
}

void StatsTracker::recordStateUpdate(const ExecutionState &state,
                                     bool terminated, bool set_stamp,
                                     data::ExecutionState *es_data) {
  if (set_stamp)
    es_data->set_time_stamp(sys::TimeValue::now().toPosixTime());

  es_data->set_id(state.state_id, 20);
  es_data->set_parent_id(state.parent_id, 20);

  es_data->set_instructions(state.totalInstructions);
  es_data->set_branches(state.totalBranches);
  es_data->set_forks(state.totalForks);
  es_data->set_queries(state.totalQueries);
  es_data->set_total_time(state.totalTime);

  es_data->set_terminated(terminated);
}

void StatsTracker::flushAllStates() {
  for (std::set<ExecutionState*>::iterator it = currentStates.begin(),
      ie = currentStates.end(); it != ie; ++it) {
    data::ExecutionState *es_data = currentStatesSet.add_execution_state();
    recordStateUpdate(*(*it), false, true, es_data);
  }
}
