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
    CLOUD9_DEBUG("CRC check failure! Ignoring, though...");
  }

  assert(this->programName == programName);
  if (this->statIDCount != statIDCount) {
    CLOUD9_DEBUG("StatIDCount Mismatch! Required: " << this->statIDCount << " Reported: " << statIDCount);
  }
  assert(this->statIDCount == statIDCount);
}

unsigned LoadBalancer::registerWorker(const std::string &address, int port,
    bool wantsUpdates) {
  assert(workers[nextWorkerID] == NULL);

  Worker *worker = new Worker();
  worker->id = nextWorkerID;
  worker->address = address;
  worker->port = port;
  worker->_wantsUpdates = wantsUpdates;

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

  reqDetails.erase(id);
  reqTransfer.erase(id);
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

    if ((**node).workerData.size() > 1) {
      // Request details from all parts
      for (std::map<int, TreeNodeInfo::WorkerInfo>::iterator it =
          (**node).workerData.begin(); it != (**node).workerData.end(); it++) {

        reqDetails.insert((*it).first);
      }
    }
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
    CLOUD9_INFO("Round " << rounds << " finished.");
    displayStatistics();

    reports.clear();
    rounds++;
  }

  if (rounds == BalanceRate) {
    rounds = 0;
    analyzeBalance();
  }
}

void LoadBalancer::displayStatistics() {
  unsigned int totalJobs = 0;

  CLOUD9_INFO("================================================================");
  for (std::map<worker_id_t, Worker*>::iterator wIt = workers.begin();
      wIt != workers.end(); wIt++) {
    Worker *worker = wIt->second;

    CLOUD9_INFO("[" << worker->getTotalJobs() << "] for worker " << worker->id);

    totalJobs += worker->getTotalJobs();
  }
  CLOUD9_INFO("----------------------------------------------------------------");
  CLOUD9_INFO("[" << totalJobs << "] IN TOTAL");
  CLOUD9_INFO("================================================================");
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
      CLOUD9_WRK_INFO(worker, "Worker timed out. Destroying");
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
        CLOUD9_INFO("LOAD BALANCING DISABLED");
      }
    }
  }

  // Handling worry timeout...

  worryTimer += TimerRate;

  if (worryTimer >= WorryRate) {
    worryTimer = 0;

    for (unsigned int i = 0; i < worries.size(); i++) {
      CLOUD9_INFO("Still waiting for a report from worker " << worries[i]);
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

void LoadBalancer::analyzeBalance() {
  if (workers.size() < 2) {
    return;
  }

  std::vector<Worker*> wList;
  Worker::LoadCompare comp;

  // TODO: optimize this further
  for (std::map<worker_id_t, Worker*>::iterator it = workers.begin(); it
      != workers.end(); it++) {
    wList.push_back((*it).second);
  }

  std::sort(wList.begin(), wList.end(), comp);

  // Compute average and deviation
  unsigned loadAvg = 0;
  unsigned sqDeviation = 0;

  for (std::vector<Worker*>::iterator it = wList.begin(); it != wList.end(); it++) {
    loadAvg += (*it)->totalJobs;
  }

  if (loadAvg == 0) {
    done = true;
    return;
  }

  if (BalanceTimeOut != 0 && balanceTimer > BalanceTimeOut)
    return;

  CLOUD9_INFO("Performing load balancing");

  loadAvg /= wList.size();

  for (std::vector<Worker*>::iterator it = wList.begin(); it != wList.end(); it++) {
    sqDeviation += (loadAvg - (*it)->totalJobs) * (loadAvg - (*it)->totalJobs);
  }

  sqDeviation /= workers.size() - 1;

  std::vector<Worker*>::iterator lowLoadIt = wList.begin();
  std::vector<Worker*>::iterator highLoadIt = wList.end() - 1;

  while (lowLoadIt < highLoadIt) {
    if (reqTransfer.count((*lowLoadIt)->id) > 0) {
      lowLoadIt++;
      continue;
    }
    if (reqTransfer.count((*highLoadIt)->id) > 0) {
      highLoadIt--;
      continue;
    }

    if ((*lowLoadIt)->totalJobs * 10 < loadAvg) {
      TransferRequest *req = computeTransfer((*highLoadIt)->id,
          (*lowLoadIt)->id,
          ((*highLoadIt)->totalJobs - (*lowLoadIt)->totalJobs) / 2);

      reqTransfer[(*lowLoadIt)->id] = req;
      reqTransfer[(*highLoadIt)->id] = req;

      highLoadIt--;
      lowLoadIt++;
      continue;
    } else
      break; // The next ones will have a larger load anyway
  }

}

TransferRequest *LoadBalancer::computeTransfer(worker_id_t fromID,
    worker_id_t toID, unsigned count) {
  // XXX Be more intelligent
  TransferRequest *req = new TransferRequest(fromID, toID);

  req->counts.push_back(count);

  std::vector<LBTree::Node*> nodes;
  nodes.push_back(tree->getRoot());

  req->paths = tree->buildPathSet(nodes.begin(), nodes.end(),
      (std::map<LBTree::Node*,unsigned>*)NULL);

  CLOUD9_DEBUG("Created transfer request from " << fromID << " to " <<
      toID << " for " << count << " states");

  return req;
}

}

}
