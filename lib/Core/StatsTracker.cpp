//===-- StatsTracker.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "StatsTracker.h"

#include "klee/ExecutionState.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"

#include "CallPathManager.h"
#include "CoreStats.h"
#include "klee/Executor.h"
#include "MemoryManager.h"
#include "UserSearcher.h"
#include "../Solver/SolverStats.h"

#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/InlineAsm.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CFG.h"
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 9)
#include "llvm/System/Process.h"
#else
#include "llvm/Support/Process.h"
#endif
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 9)
#include "llvm/System/Path.h"
#else
#include "llvm/Support/Path.h"
#endif

#include "cloud9/instrum/InstrumentationManager.h"
#include "cloud9/worker/WorkerCommon.h"

#include <iostream>
#include <fstream>

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

  cl::opt<double>
  StatsWriteInterval("stats-write-interval",
                     cl::desc("Approximate number of seconds between stats writes (default: 1.0)"),
                     cl::init(1.));

  cl::opt<double>
  IStatsWriteInterval("istats-write-interval",
                      cl::desc("Approximate number of seconds between istats writes (default: 10.0)"),
                      cl::init(10.));

  cl::opt<double>
  CoverageUpdateInterval("cov-update-interval",
      cl::desc("Approx. no. of secs between coverage updates (default: 3.0)"),
      cl::init(3.0));

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
  
  cl::opt<bool>
  DebugCoverableInstr("debug-coverable-instr", cl::init(false));

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
    
    void run() { statsTracker->writeIStats(); }
  };
  
  class WriteStatsTimer : public Executor::Timer {
    StatsTracker *statsTracker;
    
  public:
    WriteStatsTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~WriteStatsTimer() {}
    
    void run() { statsTracker->writeStatsLine(); }
  };

  class UpdateCoverageTimer: public Executor::Timer {
    StatsTracker *statsTracker;
  public:
    UpdateCoverageTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    ~UpdateCoverageTimer() { }

    void run() { statsTracker->computeCodeCoverage(); }
  };

  class UpdateReachableTimer : public Executor::Timer {
    StatsTracker *statsTracker;
    
  public:
    UpdateReachableTimer(StatsTracker *_statsTracker) : statsTracker(_statsTracker) {}
    
    void run() { statsTracker->computeReachableUncovered(); }
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

StatsTracker::StatsTracker(Executor &_executor, std::string _objectFilename,
                           bool _updateMinDistToUncovered)
  : executor(_executor),
    objectFilename(_objectFilename),
    statsFile(0),
    istatsFile(0),
    startWallTime(util::getWallTime()),
    numBranches(0),
    fullBranches(0),
    partialBranches(0),
    updateMinDistToUncovered(_updateMinDistToUncovered) {
  KModule *km = executor.kmodule;

  sys::Path module(objectFilename);
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
	if (!sys::Path(objectFilename).isAbsolute()) {
#else
  if (!llvm::sys::path::is_absolute(Twine(objectFilename))) {
#endif
    sys::Path current = sys::Path::GetCurrentDirectory();
    current.appendComponent(objectFilename);
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
    if (current.exists())
#else
    if (current.isValid())
#endif
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
        if (kf->trackCoverage && instructionIsCoverable(ki->inst)) {
          if (ki->originallyCovered) {
            ++stats::locallyCoveredInstructions;
            ++stats::globallyCoveredInstructions;
          } else {
            ++stats::locallyUncoveredInstructions;
            ++stats::globallyUncoveredInstructions;
          }
        }
      }
      
      if (kf->trackCoverage) {
        if (BranchInst *bi = dyn_cast<BranchInst>(ki->inst))
          if (!bi->isUnconditional())
            numBranches++;
      }
    }
  }

  if (DebugCoverableInstr) {
    std::stringstream ss;

    for (std::vector<KFunction*>::iterator it = km->functions.begin(),
        ie = km->functions.end(); it != ie; ++it) {
      KFunction *kf = *it;
      for (unsigned i = 0; i < kf->numInstructions; ++i) {
        KInstruction *ki = kf->instructions[i];
        unsigned id = ki->info->id;
        if (kf->trackCoverage && instructionIsCoverable(ki->inst)) {
          ss << id << " ";
        }
      }
    }

    CLOUD9_DEBUG("Coverable instruction IDs: " << ss.str());
  }

  if (OutputStats) {
    statsFile = executor.interpreterHandler->openOutputFile("run.stats");
    assert(statsFile && "unable to open statistics trace file");
    writeStatsHeader();
    writeStatsLine();

    executor.addTimer(new WriteStatsTimer(this), StatsWriteInterval);

    if (updateMinDistToUncovered) {
      computeReachableUncovered();
      executor.addTimer(new UpdateReachableTimer(this), UncoveredUpdateInterval);
    }
  }

  if (OutputIStats) {
    istatsFile = executor.interpreterHandler->openOutputFile("run.istats");
    assert(istatsFile && "unable to open istats file");

    executor.addTimer(new WriteIStatsTimer(this), IStatsWriteInterval);
  }

  executor.addTimer(new UpdateCoverageTimer(this), CoverageUpdateInterval);
}

StatsTracker::~StatsTracker() {  
  if (statsFile)
    delete statsFile;
  if (istatsFile)
    delete istatsFile;
}

void StatsTracker::done() {
  computeCodeCoverage();
  if (statsFile)
    writeStatsLine();
  if (OutputIStats)
    writeIStats();
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

		if (sf.kf->trackCoverage && instructionIsCoverable(inst)) {
			if (!theStatisticManager->getIndexedValue(
					stats::locallyCoveredInstructions, ii.id)) {
				// Checking for actual stoppoints avoids inconsistencies due
				// to line number propogation.
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)
				if (isa<DbgStopPointInst> (inst))
#endif
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

    if (updateMinDistToUncovered) {
      uint64_t minDistAtRA = 0;
      if (parentFrame)
        minDistAtRA = parentFrame->minDistToUncoveredOnReturn;

      frame->minDistToUncoveredOnReturn = frame->caller ?
        computeMinDistToUncovered(frame->caller, minDistAtRA) : 0;
    }
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
      of << "fn=" << fnIt->getNameStr() << "\n";
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
                of << "cfn=" << f->getNameStr() << "\n";
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
      } else {
        functionShortestPath[fnIt] = 0;
      }

      // Not sure if I should bother to preorder here. XXX I should.
      for (Function::iterator bbIt = fnIt->begin(), bb_ie = fnIt->end(); 
           bbIt != bb_ie; ++bbIt) {
        for (BasicBlock::iterator it = bbIt->begin(), ie = bbIt->end(); 
             it != ie; ++it) {
          instructions.push_back(it);
          unsigned id = infos.getInfo(it).id;
          sm.setIndexedValue(stats::minDistToReturn, 
                             id, 
                             isa<ReturnInst>(it) || isa<UnwindInst>(it));
        }
      }
    }
  
    std::reverse(instructions.begin(), instructions.end());
    
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
  for (Module::iterator fnIt = m->begin(), fn_ie = m->end(); 
       fnIt != fn_ie; ++fnIt) {
    // Not sure if I should bother to preorder here.
    for (Function::iterator bbIt = fnIt->begin(), bb_ie = fnIt->end(); 
         bbIt != bb_ie; ++bbIt) {
      for (BasicBlock::iterator it = bbIt->begin(), ie = bbIt->end(); 
           it != ie; ++it) {
        unsigned id = infos.getInfo(it).id;
        instructions.push_back(&*it);
        sm.setIndexedValue(stats::minDistToGloballyUncovered, 
                           id, 
                           sm.getIndexedValue(stats::globallyUncoveredInstructions, id));
      }
    }
  }
  
  std::reverse(instructions.begin(), instructions.end());
  
  // I'm so lazy it's not even worklisted.
  bool changed;
  do {
    changed = false;
    for (std::vector<Instruction*>::iterator it = instructions.begin(),
           ie = instructions.end(); it != ie; ++it) {
      Instruction *inst = *it;
      uint64_t best, cur = best = sm.getIndexedValue(stats::minDistToGloballyUncovered, 
                                                     infos.getInfo(inst).id);
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
}

void StatsTracker::computeCodeCoverage() {
  KModule *km = executor.kmodule;
  unsigned localCount = 0;
  unsigned globalCount = 0;
  unsigned grandTotal = 0;

  for (std::vector<KFunction*>::iterator it = km->functions.begin();
      it != km->functions.end(); it++) {
    if (!(*it)->trackCoverage)
      continue;

    std::pair<std::pair<unsigned, unsigned>, unsigned> cov = computeCodeCoverage(*it);

    localCount += cov.first.first;
    globalCount += cov.first.second;

    grandTotal += cov.second;
  }

  cloud9::instrum::theInstrManager.updateCoverage("<local>", std::make_pair(localCount, grandTotal));
  cloud9::instrum::theInstrManager.updateCoverage("<global>", std::make_pair(globalCount, grandTotal));

  CLOUD9_DEBUG("Code coverage is " << localCount << "/" << grandTotal << " (local) and " << globalCount << "/" << grandTotal << " (global)...");
}

std::pair<std::pair<unsigned, unsigned>, unsigned> StatsTracker::computeCodeCoverage(KFunction *kf) {
  unsigned localCount = 0;
  unsigned globalCount = 0;
  unsigned grandTotal = 0;

  for (unsigned i = 0; i < kf->numInstructions; i++) {
    unsigned id = kf->instructions[i]->info->id;

    if (!instructionIsCoverable(kf->instructions[i]->inst))
      continue;

    localCount += theStatisticManager->getIndexedValue(stats::locallyCoveredInstructions, id);

    globalCount += theStatisticManager->getIndexedValue(stats::globallyCoveredInstructions, id);
    grandTotal++;
  }

  cloud9::instrum::theInstrManager.updateCoverage(kf->function->getNameStr(),
      std::make_pair(UseGlobalCoverage ? globalCount : localCount, grandTotal));

  return std::make_pair(std::make_pair(localCount, globalCount), grandTotal);
}

