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

#include "cloud9/worker/FIStrategy.h"

#include "cloud9/worker/TreeObjects.h"

namespace cloud9 {

namespace worker {

FIStrategy::FIStrategy(WorkerTree *_workerTree, JobManager *_jobManager) :
    BasicStrategy(_jobManager), workerTree(_workerTree) {

}

unsigned FIStrategy::countInjections(SymbolicState *s, WorkerTree::Node *root, bool &interesting) {
  interesting = true;
  unsigned count = 0;

  WorkerTree::Node *crtNode = s->getNode().get();
  assert(crtNode->layerExists(WORKER_LAYER_STATES));

  while (crtNode != root) {
    WorkerTree::Node *pNode = crtNode->getParent();

    klee::ForkTag tag = (**pNode).getForkTag();

    if (tag.forkClass == klee::KLEE_FORK_FAULTINJ) {
      if (crtNode->getIndex() == 1) {
        count++;

        if (!tag.fiVulnerable)
          interesting = false;
      }
    }

    crtNode = pNode;
  }

  return count;
}

ExecutionJob* FIStrategy::onNextJobSelection() {
  if (interesting.size() > 0) {
    //CLOUD9_DEBUG("Selecting interesting state with FI counter " << interesting.begin()->first);

    assert(interesting.begin()->second.size() > 0);
    return selectJob(workerTree, *interesting.begin()->second.begin());
  } else if (uninteresting.size() > 0) {
    //CLOUD9_DEBUG("Selecting uninteresting state with FI counter " << uninteresting.begin()->first);

    assert(uninteresting.begin()->second.size() > 0);
    return selectJob(workerTree, *uninteresting.begin()->second.begin());
  } else {
    //CLOUD9_DEBUG("No more states to select...");
    return NULL;
  }
}

void FIStrategy::mapState(SymbolicState *state, unsigned count, bool isInt) {
  if (isInt) {
    interesting[count].insert(state);
    //CLOUD9_DEBUG("Interesting state mapped on " << count);
  } else {
    uninteresting[count].insert(state);
    //CLOUD9_DEBUG("Uninteresting state mapped on " << count);
  }
}

void FIStrategy::unmapState(SymbolicState *state, unsigned count) {
  unsigned res = 0;
  res += interesting[count].erase(state);
  res += uninteresting[count].erase(state);
  assert(res == 1);

  if (interesting[count].empty())
    interesting.erase(count);
  if (uninteresting[count].empty())
    uninteresting.erase(count);
}

void FIStrategy::onStateActivated(SymbolicState *state) {
  bool isInt;
  unsigned count = countInjections(state, workerTree->getRoot(), isInt);
  fiCounters[state] = count;

  mapState(state, count, isInt);
}

void FIStrategy::onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) {
  bool isInt;

  unsigned count = countInjections(state, oldNode, isInt);

  if (count == 0)
    return;

  //CLOUD9_DEBUG("State updated!");

  unsigned oldCount = fiCounters[state];
  count += oldCount;

  fiCounters[state] = count;

  isInt = isInt && (interesting.count(oldCount) > 0 && interesting[oldCount].count(state) > 0);

  unmapState(state, oldCount);
  mapState(state, count, isInt);
}

void FIStrategy::onStateDeactivated(SymbolicState *state) {
  //CLOUD9_DEBUG("Removing state...");

  unsigned count = fiCounters[state];
  fiCounters.erase(state);

  unmapState(state, count);
}

}

}
