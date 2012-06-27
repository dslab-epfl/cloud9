/*
 * PartitioningStrategy.cpp
 *
 *  Created on: Feb 18, 2011
 *      Author: stefan
 */

#include "cloud9/worker/PartitioningStrategy.h"
#include "cloud9/worker/ComplexStrategies.h"
#include "cloud9/worker/TreeObjects.h"
#include "cloud9/worker/WorkerCommon.h"
#include "cloud9/worker/JobManager.h"

#include "klee/Internal/ADT/RNG.h"
#include "klee/Searcher.h"
#include "klee/Executor.h"

#include <glog/logging.h>

using namespace klee;

namespace cloud9 {

namespace worker {

////////////////////////////////////////////////////////////////////////////////
// PartitioningStrategy
////////////////////////////////////////////////////////////////////////////////

StatePartition PartitioningStrategy::createPartition(part_id_t partID) {
  return StatePartition(createStrategy(partID));
}

StateSelectionStrategy *PartitioningStrategy::createStrategy(
    part_id_t partID) {
  return job_manager_->createBaseStrategy();
}

void PartitioningStrategy::activateStateInPartition(SymbolicState *state,
    part_id_t partID, StatePartition &part) {

  if (part.activeStates.insert(state).second) {
    part.strategy->onStateActivated(state);
  }

  active_states_.insert(state);
  if (non_empty_.insert(partID).second) {
    part.active = true; // Activate by default new partitions
    partitionCreated(partID, part);
  }
}

void PartitioningStrategy::deactivateStateInPartition(SymbolicState *state,
    part_id_t partID, StatePartition &part) {
  if (part.activeStates.erase(state)) {
    part.strategy->onStateDeactivated(state);
  } else {
    LOG(WARNING) << "Cannot find state " << state->getKleeState() << " in partition " << partID;
  }

  active_states_.erase(state);

  if (part.activeStates.empty()) {
    partitionDestroyed(partID, part);
    non_empty_.erase(partID);
  }
}

std::pair<StatePartition&, bool> PartitioningStrategy::updateStatePartition(SymbolicState *state) {
  part_id_t newKey = hashState(state);
  part_id_t oldKey = states_[state];
  StatePartition &oldPart = partitions_.find(oldKey)->second;
  if (newKey == oldKey) {
    return std::pair<StatePartition&, bool>(oldPart, false);
  }

  states_[state] = newKey;
  deactivateStateInPartition(state, oldKey, oldPart);
  oldPart.states.erase(state);

  if (partitions_.count(newKey) == 0) {
    partitions_.insert(std::make_pair(newKey, createPartition(newKey)));
  }

  StatePartition &newPart = partitions_.find(newKey)->second;
  newPart.states.insert(state);
  // By default, the state is active - it may be deactivated when the
  // child state is added
  activateStateInPartition(state, newKey, newPart);

  return std::pair<StatePartition&, bool>(newPart, true);
}

void PartitioningStrategy::onStateActivated(SymbolicState *state) {
  part_id_t key = hashState(state);
  states_[state] = key;

  if (partitions_.count(key) == 0) {
    partitions_.insert(std::make_pair(key, createPartition(key)));
  }
  StatePartition &part = partitions_.find(key)->second;

  part.states.insert(state);

  // Now decide whether to activate the state or not
  if (part.active) {
    activateStateInPartition(state, key, part);
  } else {
    if (part.activeStates.count(state->getParent()) > 0) {
      if (theRNG.getBool()) {
        // Remove the parent from the active states, and replace it with the child
        deactivateStateInPartition(state->getParent(), key, part);
        activateStateInPartition(state, key, part);
      }
    } else {
      activateStateInPartition(state, key, part);
    }
  }
}

void PartitioningStrategy::onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) {
  std::pair<StatePartition&, bool> partInfo = updateStatePartition(state);
  if (!partInfo.second) {
    partInfo.first.strategy->onStateUpdated(state, oldNode);
  }
}

void PartitioningStrategy::onStateDeactivated(SymbolicState *state) {
  part_id_t key = hashState(state);
  states_.erase(state);

  StatePartition &part = partitions_.find(key)->second;

  deactivateStateInPartition(state, key, part);
  part.states.erase(state);
}

void PartitioningStrategy::onStateStepped(SymbolicState *state) {
  std::pair<StatePartition&, bool> partInfo = updateStatePartition(state);
  if (!partInfo.second) {
    partInfo.first.strategy->onStateStepped(state);
  }
}

SymbolicState *PartitioningStrategy::selectState(part_id_t partID) {
  StatePartition &part = partitions_.find(partID)->second;
  SymbolicState *state = part.strategy->onNextStateSelection();

  if (state == NULL) {
    job_manager_->dumpSymbolicTree(job_manager_->getTree()->getRoot(),
        PartitioningDecorator(this, state->getNode().get()));
  }
  assert(state != NULL);
  if (!part.activeStates.count(state)) {
    LOG(INFO) << "Orphan state selected for partition " << partID;
  }

  return state;
}

void PartitioningStrategy::getStatistics(part_stats_t &stats) {
  std::stringstream ss;

  for (part_id_set_t::iterator it = non_empty_.begin(); it != non_empty_.end(); it++) {
    StatePartition &part = partitions_.find(*it)->second;

    stats.insert(std::make_pair(*it, std::make_pair(part.states.size(),
        part.activeStates.size())));

    ss << '[' << *it << ": " << part.activeStates.size() << '/' << part.states.size() << "] ";
  }

  LOG(INFO) << "State Partition: " << ss.str();
}

void PartitioningStrategy::setActivation(std::set<part_id_t> &activation) {
  for (std::set<part_id_t>::iterator it = activation.begin();
      it != activation.end(); it++) {
    StatePartition &part = partitions_.find(*it)->second;

    part.active = true;
  }
}

ExecutionPathSetPin PartitioningStrategy::selectStates(part_select_t &counts) {
  job_manager_->lockJobs();

  std::vector<WorkerTree::Node*> stateRoots;

  for (part_select_t::iterator it = counts.begin();
      it != counts.end(); it++) {
    if (!it->second)
      continue;

    StatePartition &part = partitions_.find(it->first)->second;
    std::set<SymbolicState*> inactiveStates;
    getInactiveSet(it->first, inactiveStates);

    unsigned int inactiveCount = it->second;
    if (inactiveStates.size() < it->second) {
      inactiveCount = inactiveStates.size();
    }

    // Grab the inactive states...
    if (inactiveCount > 0) {
      unsigned int counter = inactiveCount;
      for (std::set<SymbolicState*>::iterator it = inactiveStates.begin();
          it != inactiveStates.end(); it++) {
        if (*it == job_manager_->getCurrentState())
          continue;
        stateRoots.push_back((*it)->getNode().get());
        counter--;
        if (!counter) break;
      }

      inactiveCount -= counter; // Add back states that couldn't be selected
    }

    // Now grab the active ones...
    if (it->second - inactiveCount > 0) {
      unsigned int counter = it->second - inactiveCount;
      for (std::set<SymbolicState*>::iterator it = part.activeStates.begin();
          it != part.activeStates.end(); it++) {
        if (*it == job_manager_->getCurrentState())
          continue;
        stateRoots.push_back((*it)->getNode().get());
        counter--;
        if (!counter) break;
      }
    }
  }

  LOG(INFO) << "Selected " << stateRoots.size() << " from partitioning strategy.";

  ExecutionPathSetPin paths = job_manager_->getTree()->buildPathSet(stateRoots.begin(),
        stateRoots.end(), (std::map<WorkerTree::Node*,unsigned>*)NULL);

  job_manager_->unlockJobs();

  return paths;
}

void PartitioningStrategy::getInactiveSet(part_id_t partID,
    std::set<SymbolicState*> &inactiveStates) {
  StatePartition &part = partitions_.find(partID)->second;

  for (std::set<SymbolicState*>::iterator it = part.states.begin();
      it != part.states.end(); it++) {
    if (!part.activeStates.count(*it))
      inactiveStates.insert(*it);
  }
}

void PartitioningStrategy::dumpSymbolicTree(WorkerTree::Node *highlight) {
  job_manager_->dumpSymbolicTree(NULL, PartitioningDecorator(this, highlight));
}

}

}
