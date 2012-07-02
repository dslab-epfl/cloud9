//===-- Executor_States.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "MemoryManager.h"
#include "PTree.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "klee/CoreStats.h"
#include "klee/data/ExprSerializer.h"
#include "klee/Searcher.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/util/ExprPPrinter.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Process.h"

#include <sys/mman.h>
#include <glog/logging.h>

#include <algorithm>
#include <sstream>

using namespace llvm;

namespace {

cl::opt<bool>
DumpStatesOnHalt("dump-states-on-halt",
                 cl::init(false));

cl::opt<bool>
EmitAllErrors("emit-all-errors",
              cl::init(false),
              cl::desc("Generate tests cases for all errors "
                       "(default=one per (error,instruction) pair)"));

cl::opt<unsigned>
MaxForks("max-forks",
         cl::desc("Only fork this many times (-1=off)"),
         cl::init(~0u));

cl::opt<unsigned>
MaxMemory("max-memory",
          cl::desc("Refuse to fork when more above this about of memory (in MB, 0=off)"),
          cl::init(0));

cl::opt<bool>
MaxMemoryInhibit("max-memory-inhibit",
          cl::desc("Inhibit forking at memory cap (vs. random terminate)"),
          cl::init(true));

cl::opt<double>
MaxStaticForkPct("max-static-fork-pct", cl::init(1.));
cl::opt<double>
MaxStaticSolvePct("max-static-solve-pct", cl::init(1.));
cl::opt<double>
MaxStaticCPForkPct("max-static-cpfork-pct", cl::init(1.));
cl::opt<double>
MaxStaticCPSolvePct("max-static-cpsolve-pct", cl::init(1.));

cl::opt<bool>
OnlyOutputStatesCoveringNew("only-output-states-covering-new",
                            cl::init(false));

void *theMMap = 0;
unsigned theMMapSize = 0;

}

namespace klee {

RNG theRNG;

void Executor::branch(ExecutionState &state,
                      ref<Expr> condition,
                      const std::vector< std::pair< BasicBlock*, ref<Expr> > > &options,
                      std::vector< std::pair<BasicBlock*, ExecutionState*> > &branches,
                      int reason) {
  std::vector<std::pair<BasicBlock*, ref<Expr> > > targets;

  ref<Expr> isDefault = options[0].second;

  bool success;
  bool result;

  for (unsigned i = 1; i < options.size(); ++i) {
    success = false;

    ref<Expr> match = EqExpr::create(condition, options[i].second);
    isDefault = AndExpr::create(isDefault, Expr::createIsZero(match));

    // Check the feasibility of this option
		success = solver->mayBeTrue(data::SWITCH_FEASIBILITY, state, match, result);

    assert(success && "FIXME: Unhandled solver failure");

    if (result) {
      // Merge same destinations
      unsigned k;
      for (k = 0; k < targets.size(); k++) {
        if (targets[k].first == options[i].first) {
          targets[k].second = OrExpr::create(match, targets[k].second);
          break;
        }
      }

      if (k == targets.size()) {
        targets.push_back(std::make_pair(options[i].first, match));
      }
    }
  }

  // Check the feasibility of the default option
  success = false;

	success = solver->mayBeTrue(data::SWITCH_FEASIBILITY, state, isDefault, result);

  assert(success && "FIXME: Unhandled solver failure");

  if (result) {
    unsigned k;
    for (k = 0; k < targets.size(); k++) {
      if (targets[k].first == options[0].first) {
        targets[k].second = OrExpr::create(isDefault, targets[k].second);
        break;
      }
    }

    if (k == targets.size()) {
      targets.push_back(std::make_pair(options[0].first, isDefault));
    }
  }

  TimerStatIncrementer timer(stats::forkTime);
  assert(!targets.empty());

  stats::forks += targets.size()-1;
  state.totalForks += targets.size()-1;

  ForkTag tag = getForkTag(state, reason);

  // XXX do proper balance or keep random?
  for (unsigned i = 0; i < targets.size(); ++i) {
    ExecutionState *es = &state;
    ExecutionState *ns = es;
    if (i > 0) {
      ns = es->branch();

      addedStates.insert(ns);

      if (es->ptreeNode) {
        es->ptreeNode->data = 0;
        std::pair<PTree::Node*,PTree::Node*> res =
          processTree->split(es->ptreeNode, ns, es, tag);
        ns->ptreeNode = res.first;
        es->ptreeNode = res.second;
      }

      fireStateBranched(ns, es, 0, tag);
    }

    branches.push_back(std::make_pair(targets[i].first, ns));
  }

  // Add constraints at the end, in order to keep the parent state unaltered

  for (unsigned i = 0; i < targets.size(); ++i) {
		addConstraint(*branches[i].second, targets[i].second);
  }
}

Executor::StatePair
Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal,
    int reason) {
  ForkTag tag = getForkTag(current, reason);

  // TODO(bucur): Understand this, or wipe it altogether
  if (!isa<ConstantExpr>(condition) &&
      (MaxStaticForkPct!=1. || MaxStaticSolvePct != 1. ||
       MaxStaticCPForkPct!=1. || MaxStaticCPSolvePct != 1.) &&
      statsTracker->elapsed() > 60.) {
    StatisticManager &sm = *theStatisticManager;
    CallPathNode *cpn = current.stack().back().callPathNode;
    if ((MaxStaticForkPct<1. &&
         sm.getIndexedValue(stats::forks, sm.getIndex()) >
         stats::forks*MaxStaticForkPct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::forks) >
                 stats::forks*MaxStaticCPForkPct)) ||
        (MaxStaticSolvePct<1 &&
         sm.getIndexedValue(stats::solverTime, sm.getIndex()) >
         stats::solverTime*MaxStaticSolvePct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::solverTime) >
                 stats::solverTime*MaxStaticCPSolvePct))) {
      ref<ConstantExpr> value;
      bool success = solver->getValue(data::BRANCH_CONDITION_CONCRETIZATION, current, condition, value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      LOG(INFO) << "NONDETERMINISM! New constraint added!";
      addConstraint(current, EqExpr::create(value, condition));
      condition = value;
    }
  }

  Solver::PartialValidity partial_validity;
  Solver::Validity validity;
  bool success = false;

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    success = true;
    validity = CE->isFalse() ? Solver::False : Solver::True;
  } else {
		solver->setTimeout(stpTimeout);
		success = solver->evaluate(data::BRANCH_FEASIBILITY, current, condition,
															 validity);
		solver->setTimeout(0);

		if (!success) {
			current.pc() = current.prevPC();
			terminateStateEarly(current, "query timed out");
			return StatePair((klee::ExecutionState*)NULL, (klee::ExecutionState*)NULL);
		}
  }

  assert(success && "FIXME: Solver failure");


	switch (validity) {
	case Solver::True:
		partial_validity = Solver::MustBeTrue;
		break;
	case Solver::False:
		partial_validity = Solver::MustBeFalse;
		break;
	case Solver::Unknown:
		partial_validity = Solver::TrueOrFalse;
		break;
	}


  if (partial_validity==Solver::TrueOrFalse) {
    if ((MaxMemoryInhibit && atMemoryLimit) ||
        current.forkDisabled ||
        inhibitForking ||
        (MaxForks!=~0u && stats::forks >= MaxForks)) {

      if (MaxMemoryInhibit && atMemoryLimit)
        LOG_EVERY_N(WARNING, 1) << "skipping fork (memory cap exceeded)";
      else if (current.forkDisabled)
        LOG_EVERY_N(WARNING, 1) << "skipping fork (fork disabled on current path)";
      else if (inhibitForking)
        LOG_EVERY_N(WARNING, 1) << "skipping fork (fork disabled globally)";
      else
        LOG_EVERY_N(WARNING, 1) << "skipping fork (max-forks reached)";

      TimerStatIncrementer timer(stats::forkTime);
      if (theRNG.getBool()) {
        addConstraint(current, condition);
        partial_validity = Solver::MustBeTrue;
      } else {
        addConstraint(current, Expr::createIsZero(condition));
        partial_validity = Solver::MustBeFalse;
      }
    }
  }

  // XXX - even if the constraint is provable one way or the other we
  // can probably benefit by adding this constraint and allowing it to
  // reduce the other constraints. For example, if we do a binary
  // search on a particular value, and then see a comparison against
  // the value it has been fixed at, we should take this as a nice
  // hint to just use the single constraint instead of all the binary
  // search ones. If that makes sense.

  switch (partial_validity) {
  case Solver::MustBeTrue:
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "1";
      }
    }

    return StatePair(&current, (klee::ExecutionState*)NULL);

  case Solver::MustBeFalse:
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "0";
      }
    }

    return StatePair((klee::ExecutionState*)NULL, &current);

  case Solver::TrueOrFalse:
  case Solver::MayBeTrue:
  case Solver::MayBeFalse:
  case Solver::None:
  {
    TimerStatIncrementer timer(stats::forkTime);
    ExecutionState *falseState, *trueState = &current;

    ++stats::forks;
    ++current.totalForks;

    falseState = trueState->branch();
    addedStates.insert(falseState);

    if (current.ptreeNode) {
      current.ptreeNode->data = 0;
      std::pair<PTree::Node*, PTree::Node*> res =
        processTree->split(current.ptreeNode, falseState, trueState, tag);
      falseState->ptreeNode = res.first;
      trueState->ptreeNode = res.second;
    }

    if (&current == falseState)
      fireStateBranched(trueState, falseState, 1, tag);
    else
      fireStateBranched(falseState, trueState, 0, tag);

    if (!isInternal) {
      if (pathWriter) {
        falseState->pathOS = pathWriter->open(current.pathOS);
        trueState->pathOS << "1";
        falseState->pathOS << "0";
      }
      if (symPathWriter) {
        falseState->symPathOS = symPathWriter->open(current.symPathOS);
        trueState->symPathOS << "1";
        falseState->symPathOS << "0";
      }
    }

		addConstraint(*trueState, condition);
		addConstraint(*falseState, Expr::createIsZero(condition));

    return StatePair(trueState, falseState);
  }
  }
}

Executor::StatePair
Executor::fork(ExecutionState &current, int reason) {
  ExecutionState *lastState = &current;
  ForkTag tag = getForkTag(current, reason);

  ExecutionState *newState = lastState->branch();

  addedStates.insert(newState);

  if (lastState->ptreeNode) {
    lastState->ptreeNode->data = 0;
    std::pair<PTree::Node*,PTree::Node*> res =
     processTree->split(lastState->ptreeNode, newState, lastState, tag);
    newState->ptreeNode = res.first;
    lastState->ptreeNode = res.second;
  }

  fireStateBranched(newState, lastState, 0, tag);
  return StatePair(newState, lastState);
}

ForkTag Executor::getForkTag(ExecutionState &current, int forkClass) {
  ForkTag tag((ForkClass)forkClass);

  if (current.crtThreadIt == current.threads.end())
    return tag;

  tag.functionName = current.stack().back().kf->function->getName();
  tag.instrID = current.prevPC()->info->id;

  if (tag.forkClass == KLEE_FORK_FAULTINJ) {
    tag.fiVulnerable = false;
    // Check to see whether we are in a vulnerable call

    for (ExecutionState::stack_ty::iterator it = current.stack().begin();
        it != current.stack().end(); it++) {
      if (!it->caller)
        continue;

      KCallInstruction *callInst = dyn_cast<KCallInstruction>((KInstruction*)it->caller);
      assert(callInst);

      if (callInst->vulnerable) {
        tag.fiVulnerable = true;
        break;
      }
    }
  }

  return tag;
}

void Executor::finalizeRemovedStates() {
  // TODO(bucur): Memory management here is quite cumbersome. Consider
  // switching to smart pointers everywhere...

  for (std::set<ExecutionState*>::iterator
         it = removedStates.begin(), ie = removedStates.end();
       it != ie; ++it) {
    ExecutionState *es = *it;

    std::set<ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);

    if (es->ptreeNode)
      processTree->remove(es->ptreeNode);

    delete es;
  }
  removedStates.clear();
}

void Executor::updateStates(ExecutionState *current) {
  if (searcher) {
    searcher->update(current, addedStates, removedStates);
  }
  if (statsTracker) {
    statsTracker->updateStates(current, addedStates, removedStates);
  }

  for (std::set<ExecutionState*>::iterator it = removedStates.begin(),
      ie = removedStates.end(); it != ie; ++it) {
    fireStateDestroy(*it, false);
  }

  states.insert(addedStates.begin(), addedStates.end());
  addedStates.clear();

  finalizeRemovedStates();
}

bool Executor::terminateState(ExecutionState &state, bool silenced) {
  interpreterHandler->incPathsExplored();

  std::set<ExecutionState*>::iterator it = addedStates.find(&state);
  if (it == addedStates.end()) {
    state.pc() = state.prevPC();

    removedStates.insert(&state);
  } else {
    fireStateDestroy(&state, silenced);
    // never reached searcher, just delete immediately
    addedStates.erase(it);

    if (state.ptreeNode)
      processTree->remove(state.ptreeNode);

    delete &state;
  }

  return true;
}

void Executor::terminateStateEarly(ExecutionState &state,
                                   const Twine &message) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew) {
    interpreterHandler->processTestCase(state, (message + "\n").str().c_str(),
                                        "early");
    terminateState(state, false);
  } else {
    terminateState(state, true);
  }
}

void Executor::terminateStateOnExit(ExecutionState &state) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew) {
    interpreterHandler->processTestCase(state, 0, 0);
    terminateState(state, false);
  } else {
    terminateState(state, true);
  }
}

void Executor::terminateStateOnError(ExecutionState &state,
                                     const llvm::Twine &messaget,
                                     const char *suffix,
                                     const llvm::Twine &info) {
  std::string message = messaget.str();
  static std::set< std::pair<Instruction*, std::string> > emittedErrors;

  assert(state.crtThreadIt != state.threads.end());

  const InstructionInfo &ii = *state.prevPC()->info;

  if (EmitAllErrors ||
      emittedErrors.insert(std::make_pair(state.prevPC()->inst, message)).second) {
    if (ii.file != "") {
      LOG(ERROR) << ii.file.c_str() << ":" << ii.line << ": " << message.c_str();
    } else {
      LOG(ERROR) << message.c_str();
    }
    if (!EmitAllErrors)
      LOG(INFO) << "Now ignoring this error at this location";

    std::ostringstream msg;
    msg << "Error: " << message << "\n";
    if (ii.file != "") {
      msg << "File: " << ii.file << "\n";
      msg << "Line: " << ii.line << "\n";
    }
    msg << "Stack: \n";
    state.getStackTrace().dump(msg);

    std::string info_str = info.str();
    if (info_str != "")
      msg << "Info: \n" << info_str;
    interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);
    terminateState(state, false);
  } else {
    terminateState(state, true);
  }
}

void Executor::destroyStates() {
  if (DumpStatesOnHalt && !states.empty()) {
    std::cerr << "KLEE: halting execution, dumping remaining states\n";
    for (std::set<ExecutionState*>::iterator it = states.begin(), ie =
        states.end(); it != ie; ++it) {
      ExecutionState &state = **it;
      stepInstruction(state); // keep stats rolling
      terminateStateEarly(state, "execution halting");
    }
    updateStates(0);
  }

  delete processTree;
  processTree = 0;

  // hack to clear memory objects
  delete memory;
  memory = new MemoryManager();

  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();

  if (theMMap) {
    munmap(theMMap, theMMapSize);
    theMMap = 0;
  }
}

void Executor::destroyState(ExecutionState *state) {
  terminateStateEarly(*state, "cancelled");
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    assert(CE->isTrue() && "attempt to add invalid constraint");
    return;
  }

  state.addConstraint(condition);
  if (ivcEnabled)
    doImpliedValueConcretization(state, condition,
                                 ConstantExpr::alloc(1, Expr::Bool));
}

void Executor::executeEvent(ExecutionState &state, unsigned int type,
    long int value) {
  if (type == KLEE_EVENT_BREAKPOINT && value == KLEE_BRK_START_TRACING) {
    fireControlFlowEvent(&state, ::cloud9::worker::CHANGE_SHADOW_STATE);
  }

  fireEvent(&state, type, value);
}

void Executor::executeFork(ExecutionState &state, KInstruction *ki, uint64_t reason) {
  int forkClass = reason & 0xFF;
  bool canFork = false;
  // Check to see if we really should fork
  if (forkClass == KLEE_FORK_DEFAULT ||
      fireStateBranching(&state, getForkTag(state, forkClass))) {
    canFork = true;
  }

  if (canFork) {
    StatePair sp = fork(state, reason);

    // Return 0 in the original
    bindLocal(ki, *sp.second, ConstantExpr::create(0,
        getWidthForLLVMType(ki->inst->getType())));

    // Return 1 otherwise
    bindLocal(ki, *sp.first, ConstantExpr::create(1,
        getWidthForLLVMType(ki->inst->getType())));
  } else {
    bindLocal(ki, state, ConstantExpr::create(0,
        getWidthForLLVMType(ki->inst->getType())));
  }
}

void Executor::stepInState(ExecutionState *state) {
  activeState = state;

  fireControlFlowEvent(state, ::cloud9::worker::STEP);

  VLOG(5) << "Executing instruction: " << state->pc()->info->assemblyLine
      << " through state " << state << " Src: " << state->pc()->info->file
      << " Line: " << state->pc()->info->line;

  resetTimers();

	KInstruction *ki = state->pc();

	stepInstruction(*state);
	executeInstruction(*state, ki);
	state->stateTime++; // Each instruction takes one unit of time

  processTimers(state);

  if (MaxMemory) {
    if ((stats::instructions & 0xFFFF) == 0) {
      // We need to avoid calling GetMallocUsage() often because it
      // is O(elts on freelist). This is really bad since we start
      // to pummel the freelist once we hit the memory cap.
      unsigned mbs = sys::Process::GetTotalMemoryUsage() >> 20;

      if (mbs > MaxMemory) {
        if (mbs > MaxMemory + 100) {
          // just guess at how many to kill
          unsigned numStates = states.size();
          unsigned toKill = std::max(1U, numStates - numStates
              * MaxMemory / mbs);

          if (MaxMemoryInhibit)
            LOG(WARNING) << "Killing " << toKill << " states (over memory cap)";

          std::vector<ExecutionState*> arr(states.begin(),
              states.end());
          for (unsigned i = 0, N = arr.size(); N && i < toKill; ++i, --N) {
            unsigned idx = rand() % N;

            // Make two pulls to try and not hit a state that
            // covered new code.
            if (arr[idx]->coveredNew)
              idx = rand() % N;

            std::swap(arr[idx], arr[N - 1]);

            fireOutOfResources(arr[N-1]);
            terminateStateEarly(*arr[N - 1], "memory limit");
          }
        }
        atMemoryLimit = true;
      } else {
        atMemoryLimit = false;
      }
    }
  }

  updateStates(state);

  activeState = NULL;
}

void Executor::run(ExecutionState &initialState) {
  searcher = initSearcher(NULL);

  searcher->update(0, states, std::set<ExecutionState*>());

  while (!states.empty() && !haltExecution) {
    ExecutionState &state = searcher->selectState();

    stepInState(&state);
  }

  delete searcher;
  searcher = 0;

  if (DumpStatesOnHalt && !states.empty()) {
    std::cerr << "KLEE: halting execution, dumping remaining states\n";
    for (std::set<ExecutionState*>::iterator
           it = states.begin(), ie = states.end();
         it != ie; ++it) {
      ExecutionState &state = **it;
      stepInstruction(state); // keep stats rolling
      terminateStateEarly(state, "execution halting");
    }
    updateStates(0);
  }
}

}
