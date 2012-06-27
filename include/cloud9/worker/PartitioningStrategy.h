/*
 * PartitioningStrategy.h
 *
 *  Created on: Feb 18, 2011
 *      Author: stefan
 */

#ifndef PARTITIONINGSTRATEGY_H_
#define PARTITIONINGSTRATEGY_H_

#include "cloud9/worker/CoreStrategies.h"

#include <vector>
#include <set>
#include <string>

namespace cloud9 {

namespace worker {

class PartitioningStrategy;

class StatePartition {
public:
  StatePartition(StateSelectionStrategy *_strategy)
    : strategy(_strategy), active(true) { }

  virtual ~StatePartition() { }

private:
  std::set<SymbolicState*> states;
  std::set<SymbolicState*> activeStates;
  StateSelectionStrategy *strategy;
  bool active;

  friend class PartitioningStrategy;
};

typedef unsigned long part_id_t;
typedef std::set<part_id_t> part_id_set_t;
typedef std::map<part_id_t, std::pair<unsigned, unsigned> > part_stats_t;
typedef std::map<part_id_t, unsigned> part_select_t;

class PartitioningDecorator;
class JobManager;

class PartitioningStrategy: public StateSelectionStrategy {
public:
  PartitioningStrategy(JobManager *job_manager)
    : job_manager_(job_manager) { }
  virtual ~PartitioningStrategy() { }

  virtual void onStateActivated(SymbolicState *state);
  virtual void onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode);
  virtual void onStateDeactivated(SymbolicState *state);
  virtual void onStateStepped(SymbolicState *state);

  void getStatistics(part_stats_t &stats);
  void setActivation(std::set<part_id_t> &activation);
  ExecutionPathSetPin selectStates(part_select_t &counts);

protected:
  virtual part_id_t hashState(SymbolicState* state) = 0;

  virtual void dumpSymbolicTree(WorkerTree::Node *highlight);
  virtual SymbolicState *selectState(part_id_t partID);

  virtual StateSelectionStrategy *createStrategy(part_id_t partID);
  virtual void partitionCreated(part_id_t partID, StatePartition &part) { };
  virtual void partitionDestroyed(part_id_t partID, StatePartition &part) { };

  std::map<part_id_t, StatePartition> partitions() { return partitions_; }
  part_id_set_t &non_empty() { return non_empty_; }
  std::map<SymbolicState*, part_id_t> &states() { return states_; }
private:
  StatePartition createPartition(part_id_t partID);
  void activateStateInPartition(SymbolicState *state, part_id_t partID,
      StatePartition &part);
  void deactivateStateInPartition(SymbolicState *state, part_id_t partID,
      StatePartition &part);
  std::pair<StatePartition&,bool> updateStatePartition(SymbolicState *state);
  void getInactiveSet(part_id_t partID, std::set<SymbolicState*> &inactiveStates);

  std::map<SymbolicState*, part_id_t> states_;
  std::map<part_id_t, StatePartition> partitions_;
  std::set<SymbolicState*> active_states_;
  part_id_set_t non_empty_;

  JobManager *job_manager_;

  friend class PartitioningDecorator;
};

class PartitioningDecorator: public WorkerNodeDecorator {
public:
  PartitioningDecorator(PartitioningStrategy *_strategy, WorkerTree::Node *highlight) :
    WorkerNodeDecorator(highlight), strategy(_strategy) {

  }

  void operator() (WorkerTree::Node *node, deco_t &deco, edge_deco_t &inEdges) {
    WorkerNodeDecorator::operator() (node, deco, inEdges);

    SymbolicState *state = (**node).getSymbolicState();

    if (state && strategy->states_.count(state) > 0) {
      char label[10];
      snprintf(label, 10, "%d", strategy->states_[state]);
      deco["label"] = std::string(label);
    }
  }

private:
  PartitioningStrategy *strategy;

};

}

}

#endif /* PARTITIONINGSTRATEGY_H_ */
