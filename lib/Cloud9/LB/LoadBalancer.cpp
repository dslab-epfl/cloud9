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

#include "cloud9/lb/LoadBalancer.h"
#include "cloud9/lb/Worker.h"
#include "cloud9/lb/LBCommon.h"

#include "llvm/Support/CommandLine.h"

#include <cassert>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <glog/logging.h>

using namespace llvm;

namespace {
cl::opt<unsigned int> BalanceRate("balance-rate",
        cl::desc("The rate at which load balancing decisions take place, measured in rounds"),
        cl::init(2));

cl::opt<unsigned int> TimerRate("timer-rate", cl::desc(
    "The rate at which the internal timer triggers, measured in seconds"),
    cl::init(1));

cl::opt<unsigned int> WorkerTimeOut("worker-tout",
    cl::desc("Timeout for worker updates"), cl::init(600));

cl::opt<unsigned int> WorkerWorry("worker-worry",
    cl::desc("Timeout after which we worry for a worker (advertise for it)"), cl::init(60));

cl::opt<unsigned int> WorryRate("worry-rate",
    cl::desc("Worry advertising rate"), cl::init(10));
}

cl::opt<unsigned int> BalanceTimeOut("balance-tout",
    cl::desc("The duration of the load balancing process"), cl::init(0));

namespace cloud9 {

namespace lb {

typedef std::pair<worker_id_t, unsigned> load_t;

struct LoadCompare {
  bool operator()(const load_t &a, const load_t &b) {
    return a.second < b.second;
  }
};

LoadBalancer::LoadBalancer(boost::asio::io_service &service) :
  timer(service), worryTimer(0), balanceTimer(0), nextWorkerID(1), rounds(0),
  done(false) {
  tree = new LBTree();

  timer.expires_from_now(boost::posix_time::seconds(TimerRate));

  timer.async_wait(boost::bind(&LoadBalancer::periodicCheck, this,
      boost::asio::placeholders::error));
}

LoadBalancer::~LoadBalancer() {
  // TODO Auto-generated destructor stub
}

void LoadBalancer::registerProgramParams(const std::string &programName,
    unsigned crc, unsigned statIDCount) {
  this->programName = programName;
  this->statIDCount = statIDCount;
  this->programCRC = crc;

  globalCoverageData.resize(statIDCount);
  globalCoverageUpdates.resize(statIDCount, false);
}

void LoadBalancer::checkProgramParams(const std::string &programName,
    unsigned crc, unsigned statIDCount) {
  if (this->programCRC != crc) {
    LOG(INFO) << "CRC check failure! Ignoring, though...";
  }

  assert(this->programName == programName);
  if (this->statIDCount != statIDCount) {
    LOG(INFO) << "StatIDCount Mismatch! Required: " << this->statIDCount << " Reported: " << statIDCount;
  }
  assert(this->statIDCount == statIDCount);
}

unsigned LoadBalancer::registerWorker(const std::string &address, int port,
    bool wantsUpdates, bool hasPartitions) {
  assert(workers[nextWorkerID] == NULL);

  Worker *worker = new Worker();
  worker->id = nextWorkerID;
  worker->address = address;
  worker->port = port;
  worker->_wantsUpdates = wantsUpdates;
  worker->_hasPartitions = hasPartitions;

  if (wantsUpdates)
    worker->globalCoverageUpdates = globalCoverageUpdates;

  workers[nextWorkerID] = worker;

  nextWorkerID++;

  return worker->id;
}

void LoadBalancer::deregisterWorker(worker_id_t id) {
  Worker *worker = workers[id];
  assert(worker);

  // Cleanup any pending information about the worker
  workers.erase(id);
  reports.erase(id);

  // Clean any pending transfer request to this worker
  for (std::map<worker_id_t, Worker*>::iterator wIt = workers.begin();
        wIt != workers.end(); wIt++) {
    Worker *worker = wIt->second;

    if (worker->transferReq && worker->globalTransfer.first == id) {
      worker->transferReq = false;
    }
    if (worker->partTransfers.size() > 0) {
      for (part_transfers_t::iterator pIt = worker->partTransfers.begin();
          pIt != worker->partTransfers.end();) {
        if (pIt->second.first == id)
          worker->partTransfers.erase(pIt++);
        else
          pIt++;
      }
    }
  }
}

void LoadBalancer::updateWorkerStatNodes(worker_id_t id, std::vector<
    LBTree::Node*> &newNodes) {
  Worker *worker = workers[id];
  assert(worker);

  int revision = worker->nodesRevision++;

  // Add the new stat nodes
  for (std::vector<LBTree::Node*>::iterator it = newNodes.begin(); it
      != newNodes.end(); it++) {
    LBTree::Node *node = *it;

    // Update upstream information
    while (node) {
      TreeNodeInfo::WorkerInfo &info = (**node).workerData[id];
      if (info.revision > 0 && info.revision == revision)
        break;

      info.revision = revision;

      node = node->getParent();
    }

    node = *it;
    assert((**node).workerData.size() >= 1);
  }

  // Remove old branches
  for (std::vector<LBTree::Node*>::iterator it = worker->nodes.begin(); it
      != worker->nodes.end(); it++) {

    LBTree::Node *node = *it;

    while (node) {
      if ((**node).workerData.find(id) == (**node).workerData.end())
        break;

      TreeNodeInfo::WorkerInfo &info = (**node).workerData[id];

      assert(info.revision > 0);

      if (info.revision == revision)
        break;

      (**node).workerData.erase(id);

      node = node->getParent();
    }
  }

  // Update the list of stat nodes
  worker->nodes = newNodes;
}

void LoadBalancer::updateWorkerStats(worker_id_t id, std::vector<int> &stats) {
  Worker *worker = workers[id];
  assert(worker);

  //assert(stats.size() == worker->nodes.size());

  worker->totalJobs = 0;

  for (unsigned i = 0; i < stats.size(); i++) {
    // XXX Enable this at some point; for now it's useless
    //LBTree::Node *node = worker->nodes[i];

    //(**node).workerData[id].jobCount = stats[i];
    worker->totalJobs += stats[i];
  }
}

void LoadBalancer::analyze(worker_id_t id) {
  Worker *worker = workers[id];
  worker->lastReportTime = 0; // Reset the last report time
  reports.insert(id);

  if (reports.size() == workers.size()) {
    LOG(INFO) << "Round " << rounds << " finished.";
    displayStatistics();

    reports.clear();
    rounds++;
  }

  if (rounds == BalanceRate) {
    rounds = 0;

    // First, attempt partitioned load balancing
    bool result = analyzePartitionBalance();
    if (!result) {
      LOG(INFO) << "Partitions not found, falling back on global LB.";
      // Fall back on global load balancing
      analyzeAggregateBalance();
    }
  }
}

void LoadBalancer::displayStatistics() {
  unsigned int totalJobs = 0;

  LOG(INFO) << "================================================================";
  for (std::map<worker_id_t, Worker*>::iterator wIt = workers.begin();
      wIt != workers.end(); wIt++) {
    Worker *worker = wIt->second;

    LOG(INFO) << "[" << worker->getTotalJobs() << "] for worker " << worker->id;

    totalJobs += worker->getTotalJobs();
  }
  LOG(INFO) << "----------------------------------------------------------------";
  LOG(INFO) << "[" << totalJobs << "] IN TOTAL";
  LOG(INFO) << "================================================================";
}

void LoadBalancer::periodicCheck(const boost::system::error_code& error) {
  std::vector<worker_id_t> timedOut;
  std::vector<worker_id_t> worries;

  // Handling worker reply timeouts...

  for (std::map<worker_id_t, Worker*>::iterator wIt = workers.begin();
      wIt != workers.end(); wIt++) {
    Worker *worker = wIt->second;

    worker->lastReportTime += TimerRate;

    if (worker->lastReportTime > WorkerTimeOut) {
      LOG(INFO) << "Worker timed out. Destroying";
      timedOut.push_back(worker->id);
    } else if (worker->lastReportTime > WorkerWorry) {
      worries.push_back(worker->id);
    }
  }

  for (unsigned int i = 0; i < timedOut.size(); i++) {
    deregisterWorker(timedOut[i]);
  }

  // Handling load balancing timeout...

  if (BalanceTimeOut > 0) {
    if (balanceTimer <= BalanceTimeOut) {
      balanceTimer += TimerRate;

      if (balanceTimer > BalanceTimeOut) {
        LOG(INFO) << "LOAD BALANCING DISABLED";
      }
    }
  }

  // Handling worry timeout...

  worryTimer += TimerRate;

  if (worryTimer >= WorryRate) {
    worryTimer = 0;

    for (unsigned int i = 0; i < worries.size(); i++) {
      LOG(INFO) << "Still waiting for a report from worker " << worries[i];
    }
  }

  timer.expires_at(timer.expires_at() + boost::posix_time::seconds(TimerRate));

  timer.async_wait(boost::bind(&LoadBalancer::periodicCheck, this,
      boost::asio::placeholders::error));
}

void LoadBalancer::updateCoverageData(worker_id_t id, const cov_update_t &data) {
  for (cov_update_t::const_iterator it = data.begin(); it != data.end(); it++) {
    globalCoverageUpdates[it->first] = true;
    globalCoverageData[it->first] = it->second;
  }

  for (std::map<worker_id_t, Worker*>::iterator wIt = workers.begin(); wIt
      != workers.end(); wIt++) {
    if (wIt->first == id || !wIt->second->_wantsUpdates)
      continue;

    for (cov_update_t::const_iterator it = data.begin(); it != data.end(); it++) {
      wIt->second->globalCoverageUpdates[it->first] = true;
    }
  }
}

void LoadBalancer::updatePartitioningData(worker_id_t id, const part_stat_t &stats) {
  Worker *worker = workers[id];
  assert(worker);

  worker->statePartitions = stats;
}

void LoadBalancer::getAndResetCoverageUpdates(worker_id_t id,
    cov_update_t &data) {
  Worker *w = workers[id];

  for (unsigned i = 0; i < w->globalCoverageUpdates.size(); i++) {
    if (w->globalCoverageUpdates[i]) {
      data.push_back(std::make_pair(i, globalCoverageData[i]));
      w->globalCoverageUpdates[i] = false;
    }
  }
}

bool LoadBalancer::requestAndResetTransfer(worker_id_t id, transfer_t &globalTrans,
      part_transfers_t &partTrans) {
  Worker *w = workers[id];

  if (!w->transferReq && w->partTransfers.empty()) {
    return false;
  }

  if (w->transferReq)
    globalTrans = w->globalTransfer;
  if (!w->partTransfers.empty())
    partTrans = w->partTransfers;

  w->transferReq = false;
  w->partTransfers.clear();

  return true;
}

bool LoadBalancer::analyzeBalance(std::map<worker_id_t, unsigned> &load,
      std::map<worker_id_t, transfer_t> &xfers, unsigned balanceThreshold,
      unsigned minTransfer) {
  if (load.size() < 2)
    return true;

  std::vector<load_t> loadVec;
  loadVec.insert(loadVec.begin(), load.begin(), load.end());

  std::sort(loadVec.begin(), loadVec.end(), LoadCompare());

  // Compute average and deviation
  unsigned loadAvg = 0;
  unsigned sqDeviation = 0;

  for (std::vector<load_t>::iterator it = loadVec.begin(); it != loadVec.end(); it++) {
    loadAvg += it->second;
  }

  if (loadAvg == 0) {
    return false;
  }

  loadAvg /= loadVec.size();

  for (std::vector<load_t>::iterator it = loadVec.begin(); it != loadVec.end(); it++) {
    sqDeviation += (loadAvg - it->second) * (loadAvg - it->second);
  }

  sqDeviation /= loadVec.size() - 1;

  std::vector<load_t>::iterator lowLoadIt = loadVec.begin();
  std::vector<load_t>::iterator highLoadIt = loadVec.end() - 1;

  while (lowLoadIt < highLoadIt) {
    unsigned xferCount = (highLoadIt->second - lowLoadIt->second) / 2;
    if (lowLoadIt->second * balanceThreshold <= loadAvg && xferCount >= minTransfer) {
      xfers[highLoadIt->first] = std::make_pair(lowLoadIt->first, xferCount);
      highLoadIt--;
      lowLoadIt++;
      continue;
    } else
      break; // The next ones will have a larger load anyway
  }

  return true;
}

void LoadBalancer::analyzeAggregateBalance() {
  if (workers.size() < 2) {
    return;
  }

  std::map<worker_id_t, unsigned> load;
  std::map<worker_id_t, transfer_t> xfers;

  for (std::map<worker_id_t, Worker*>::iterator it = workers.begin(); it
      != workers.end(); it++) {
    load[it->first] = it->second->totalJobs;
  }

  bool result = analyzeBalance(load, xfers, 10, 1);

  if (!result) {
    done = true;
    return;
  }

  LOG(INFO) << "Performing load balancing";

  if (xfers.size() > 0) {
    for (std::map<worker_id_t, transfer_t>::iterator it = xfers.begin();
        it != xfers.end(); it++) {
      Worker *worker = workers[it->first];
      if (worker->transferReq) {
        // Skip this one...
        continue;
      }

      worker->transferReq = true;
      worker->globalTransfer = it->second;

      LOG(INFO) << "Created transfer request from " << it->first << " to " <<
            it->second.first << " for " << it->second.second << " states";
    }
  }
}

bool LoadBalancer::analyzePartitionBalance() {
  // Compute the aggregate situation
  if (workers.size() < 2) {
    return true;
  }

  part_stat_t globalPart;
  bool hasPartitions = false;

  for (std::map<worker_id_t, Worker*>::iterator wit = workers.begin();
      wit != workers.end(); wit++) {
    part_stat_t &part = wit->second->statePartitions;
    hasPartitions |= wit->second->hasPartitions();

    for (part_stat_t::iterator pit = part.begin(); pit != part.end(); pit++) {
      std::pair<unsigned, unsigned> curGlobal = globalPart[pit->first];
      globalPart[pit->first] = std::make_pair(curGlobal.first + pit->second.first,
          curGlobal.second + pit->second.second);
    }
  }

  if (!hasPartitions) {
    return false;
  }

  if (globalPart.empty()) {
    done = true;
    return true;
  }

  // Perform load balancing along all partitions
  for (part_stat_t::iterator pit = globalPart.begin(); pit != globalPart.end();
      pit++) {

    std::map<worker_id_t, unsigned> load;
    std::map<worker_id_t, transfer_t> xfers;
    for (std::map<worker_id_t, Worker*>::iterator it = workers.begin();
        it != workers.end(); it++) {
      load[it->first] = it->second->statePartitions[pit->first].second;
    }

    analyzeBalance(load, xfers, 10, 1);

    if (xfers.empty()) {
      // Nothing to see here... move on.
      continue;
    }

    for (std::map<worker_id_t, transfer_t>::iterator it = xfers.begin();
        it != xfers.end(); it++) {
      Worker *worker = workers[it->first];
      if (worker->partTransfers.count(pit->first) > 0) {
        // We just skip this...
        continue;
      }
      worker->partTransfers[pit->first] = it->second;

      LOG(INFO) << "Created transfer request from " << it->first << " to " <<
          it->second.first << " for " << it->second.second <<
          " states in partition " << pit->first;
    }
  }

  return true;
}

}

}
