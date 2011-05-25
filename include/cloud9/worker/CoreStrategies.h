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

#ifndef CORESTRATEGIES_H_
#define CORESTRATEGIES_H_

#include "cloud9/worker/TreeNodeInfo.h"

#include <vector>
#include <map>

namespace klee {
class Searcher;
class ExecutionState;
}

namespace cloud9 {

namespace worker {

class ExecutionJob;
class SymbolicEngine;
class SymbolicState;
class JobManager;

////////////////////////////////////////////////////////////////////////////////
// Basic Building Blocks
////////////////////////////////////////////////////////////////////////////////

/*
 * The abstract base class for all job strategies
 */
class JobSelectionStrategy {
public:
    JobSelectionStrategy() {};
    virtual ~JobSelectionStrategy() {};

public:
    virtual void onJobAdded(ExecutionJob *job) = 0;
    virtual ExecutionJob* onNextJobSelection() = 0;
    virtual ExecutionJob* onNextJobSelectionEx(bool &canBatch, uint32_t &batchDest) {
      return onNextJobSelection();
    }

    virtual void onRemovingJob(ExecutionJob *job) = 0;

    virtual void onStateActivated(SymbolicState *state) = 0;
    virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) = 0;
    virtual void onStateDeactivated(SymbolicState *state) = 0;
    virtual void onStateStepped(SymbolicState *state) = 0;
};

class StateSelectionStrategy {
  friend class RandomJobFromStateStrategy;
public:
  StateSelectionStrategy() { }
  virtual ~StateSelectionStrategy() { }
protected:
  virtual void dumpSymbolicTree(WorkerTree::Node *highlight) { }
public:
  virtual void onStateActivated(SymbolicState *state) { };
  virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) { };
  virtual void onStateDeactivated(SymbolicState *state) { };
  virtual void onStateStepped(SymbolicState *state) { };

  virtual SymbolicState* onNextStateSelection() = 0;
  virtual SymbolicState* onNextStateSelectionEx(bool &canBatch, uint32_t &batchDest) {
    return onNextStateSelection();
  }
};

class BasicStrategy : public JobSelectionStrategy {
protected:
  JobManager *jobManager;

  ExecutionJob *selectJob(WorkerTree *tree, SymbolicState* state);
  virtual void dumpSymbolicTree(WorkerTree::Node *highlight);
public:
    BasicStrategy(JobManager *_jobManager) : jobManager(_jobManager) { }
    virtual ~BasicStrategy() { }

public:
    virtual void onJobAdded(ExecutionJob *job) { }
    virtual ExecutionJob* onNextJobSelection() = 0;
    virtual void onRemovingJob(ExecutionJob *job) { }

    virtual void onStateActivated(SymbolicState *state) { }
    virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) { }
    virtual void onStateDeactivated(SymbolicState *state) { }
    virtual void onStateStepped(SymbolicState *state) { }
};

class RandomJobFromStateStrategy: public BasicStrategy {
private:
  WorkerTree *tree;
  StateSelectionStrategy *stateStrat;

public:
  RandomJobFromStateStrategy(WorkerTree *_tree, StateSelectionStrategy *_stateStrat,
      JobManager *_jobManager) :
    BasicStrategy(_jobManager), tree(_tree), stateStrat(_stateStrat) { }

  virtual void onStateActivated(SymbolicState *state);
  virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode);
  virtual void onStateDeactivated(SymbolicState *state);
  virtual void onStateStepped(SymbolicState *state);

  virtual ExecutionJob* onNextJobSelection();
  virtual ExecutionJob* onNextJobSelectionEx(bool &canBatch, uint32_t &batchDest);

  StateSelectionStrategy *getStateStrategy() const { return stateStrat; }
};

class RandomPathStrategy: public StateSelectionStrategy {
private:
  WorkerTree *tree;
public:
  RandomPathStrategy(WorkerTree *t) :
    tree(t) { };

  virtual ~RandomPathStrategy() { };

  virtual SymbolicState* onNextStateSelection();
};

////////////////////////////////////////////////////////////////////////////////
// State Search Strategies
////////////////////////////////////////////////////////////////////////////////

class RandomStrategy: public StateSelectionStrategy {
private:
    std::vector<SymbolicState*> states;
    std::map<SymbolicState*, unsigned> indices;
public:
    RandomStrategy() {};
    virtual ~RandomStrategy() {};

    virtual SymbolicState* onNextStateSelection();
    virtual void onStateActivated(SymbolicState *state);
    virtual void onStateDeactivated(SymbolicState *state);
};

class KleeStrategy: public StateSelectionStrategy {
protected:
    WorkerTree *tree;
    klee::Searcher *searcher;

    KleeStrategy(WorkerTree *_tree);
public:
    KleeStrategy(WorkerTree *_tree, klee::Searcher *_searcher);
    virtual ~KleeStrategy();

    virtual void onStateActivated(SymbolicState *state);
    virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode);
    virtual void onStateDeactivated(SymbolicState *state);

    virtual SymbolicState* onNextStateSelection();
};

class WeightedRandomStrategy: public KleeStrategy {
public:
    enum WeightType {
        Depth,
        QueryCost,
        InstCount,
        CPInstCount,
        MinDistToUncovered,
        CoveringNew
      };
public:
    WeightedRandomStrategy(WeightType _type, WorkerTree *_tree, SymbolicEngine *_engine);
    virtual ~WeightedRandomStrategy();

};

}

}

#endif /* CORESTRATEGIES_H_ */
