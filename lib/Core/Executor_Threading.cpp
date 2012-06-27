//===-- Executor_Threading.cpp --------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "StatsTracker.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"

#include <glog/logging.h>


using namespace llvm;

namespace {

cl::opt<bool>
DebugSchedulingHistory("debug-sched-history", cl::init(false));

cl::opt<bool>
ForkOnSchedule("fork-on-schedule",
   cl::desc("fork when various schedules are possible (defaul=disabled)"),
   cl::init(false));

cl::opt<unsigned int>
MaxPreemptions("scheduler-preemption-bound",
   cl::desc("scheduler preemption bound (default=0)"),
   cl::init(0));

}


namespace klee {

bool Executor::schedule(ExecutionState &state, bool yield) {

  int enabledCount = 0;
  for(ExecutionState::threads_ty::iterator it = state.threads.begin();
      it != state.threads.end();  it++) {
    if(it->second.enabled) {
      enabledCount++;
    }
  }

  if (enabledCount == 0) {
    terminateStateOnError(state, " ******** hang (possible deadlock?)", "user.err");
    return false;
  }

  bool forkSchedule = false;
  bool incPreemptions = false;

  ExecutionState::threads_ty::iterator oldIt = state.crtThreadIt;

  if(!state.crtThread().enabled || yield) {
    ExecutionState::threads_ty::iterator it = state.nextThread(state.crtThreadIt);

    while (!it->second.enabled)
      it = state.nextThread(it);

    state.scheduleNext(it);

    if (ForkOnSchedule)
      forkSchedule = true;
  } else {
    if (state.preemptions < MaxPreemptions) {
      forkSchedule = true;
      incPreemptions = true;
    }
  }

  if (DebugSchedulingHistory) {
    LOG(INFO) << "Context Switch: --- TID: " << state.crtThread().tuid.first <<
        " PID: " << state.crtThread().tuid.second << " -----------------------";
    unsigned int depth = state.stack().size() - 1;
    LOG(INFO) << "Call: " << std::string(depth, ' ') << state.stack().back().kf->function->getName().str();
  }

  if (forkSchedule) {
    ExecutionState::threads_ty::iterator finalIt = state.crtThreadIt;
    ExecutionState::threads_ty::iterator it = state.nextThread(finalIt);
    ExecutionState *lastState = &state;

    ForkClass forkClass = KLEE_FORK_SCHEDULE;

    while (it != finalIt) {
      // Choose only enabled states, and, in the case of yielding, do not
      // reschedule the same thread
      if (it->second.enabled && (!yield || it != oldIt)) {
        StatePair sp = fork(*lastState, forkClass);

        if (incPreemptions)
          sp.first->preemptions = state.preemptions + 1;

        sp.first->scheduleNext(sp.first->threads.find(it->second.tuid));

        lastState = sp.first;

        if (forkClass == KLEE_FORK_SCHEDULE) {
          forkClass = KLEE_FORK_MULTI;   // Avoid appearing like multiple schedules
        }
      }

      it = state.nextThread(it);
    }
  }

  return true;
}

void Executor::executeThreadCreate(ExecutionState &state, thread_id_t tid,
             ref<Expr> start_function, ref<Expr> arg)
{
  VLOG(1) << "Creating thread...";
  KFunction *kf = resolveFunction(start_function);
  assert(kf && "cannot resolve thread start function");

  Thread &t = state.createThread(tid, kf);

  bindArgumentToPthreadCreate(kf, 0, t.stack.back(), arg);

  if (statsTracker)
    statsTracker->framePushed(&t.stack.back(), 0);
}

void Executor::executeThreadExit(ExecutionState &state) {
  //terminate this thread and schedule another one
  VLOG(1) << "Exiting thread...";

  if (state.threads.size() == 1) {
    LOG(INFO) << "Terminating state";
    terminateStateOnExit(state);
    return;
  }

  assert(state.threads.size() > 1);

  ExecutionState::threads_ty::iterator thrIt = state.crtThreadIt;
  thrIt->second.enabled = false;

  if (!schedule(state, false))
    return;

  state.terminateThread(thrIt);
}

void Executor::executeThreadNotifyOne(ExecutionState &state, wlist_id_t wlist) {
  // Copy the waiting list
  std::set<thread_uid_t> wl = state.waitingLists[wlist];

  if (!ForkOnSchedule || wl.size() <= 1) {
    if (wl.size() == 0)
      state.waitingLists.erase(wlist);
    else
      state.notifyOne(wlist, *wl.begin()); // Deterministically pick the first thread in the queue

    return;
  }

  ExecutionState *lastState = &state;

  for (std::set<thread_uid_t>::iterator it = wl.begin(); it != wl.end();) {
    thread_uid_t tuid = *it++;

    if (it != wl.end()) {
      StatePair sp = fork(*lastState, KLEE_FORK_SCHEDULE);

      sp.second->notifyOne(wlist, tuid);

      lastState = sp.first;
    } else {
      lastState->notifyOne(wlist, tuid);
    }
  }
}

void Executor::executeProcessFork(ExecutionState &state, KInstruction *ki,
    process_id_t pid) {

  VLOG(1) << "Forking with pid " << pid;

  Thread &pThread = state.crtThread();

  Process &child = state.forkProcess(pid);

  Thread &cThread = state.threads.find(*child.threads.begin())->second;

  // Set return value in the child
  state.scheduleNext(state.threads.find(cThread.tuid));
  bindLocal(ki, state, ConstantExpr::create(0,
      getWidthForLLVMType(ki->inst->getType())));

  // Set return value in the parent
  state.scheduleNext(state.threads.find(pThread.tuid));
  bindLocal(ki, state, ConstantExpr::create(child.pid,
      getWidthForLLVMType(ki->inst->getType())));
}


void Executor::executeProcessExit(ExecutionState &state) {
  if (state.processes.size() == 1) {
    terminateStateOnExit(state);
    return;
  }

  VLOG(1) << "Terminating " << state.crtProcess().threads.size() << " threads of the current process...";

  ExecutionState::processes_ty::iterator procIt = state.crtProcessIt;

  // Disable all the threads of the current process
  for (std::set<thread_uid_t>::iterator it = procIt->second.threads.begin();
      it != procIt->second.threads.end(); it++) {
    ExecutionState::threads_ty::iterator thrIt = state.threads.find(*it);

    if (thrIt->second.enabled) {
      // Disable any enabled thread
      thrIt->second.enabled = false;
    } else {
      // If the thread is disabled, remove it from any waiting list
      wlist_id_t wlist = thrIt->second.waitingList;

      if (wlist > 0) {
        state.waitingLists[wlist].erase(thrIt->first);
        if (state.waitingLists[wlist].size() == 0)
          state.waitingLists.erase(wlist);

        thrIt->second.waitingList = 0;
      }
    }
  }

  if (!schedule(state, false))
    return;

  state.terminateProcess(procIt);
}

}
