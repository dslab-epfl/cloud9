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

#ifndef LOADBALANCER_H_
#define LOADBALANCER_H_

#include "cloud9/lb/TreeNodeInfo.h"
#include "cloud9/lb/Worker.h"

#include <set>
#include <map>
#include <boost/asio.hpp>

namespace cloud9 {

class ExecutionPath;

namespace lb {

// TODO: Refactor this into a more generic class
class LoadBalancer {
private:
  LBTree *tree;

  boost::asio::deadline_timer timer;
  unsigned worryTimer;
  unsigned balanceTimer;

  std::string programName;
  unsigned statIDCount;
  unsigned programCRC;

  worker_id_t nextWorkerID;

  unsigned rounds;
  bool done;

  std::map<worker_id_t, Worker*> workers;
  std::set<worker_id_t> reports;

  // TODO: Restructure this to a more intuitive representation with
  // global coverage + coverage deltas
  std::vector<uint64_t> globalCoverageData;
  std::vector<char> globalCoverageUpdates;

  bool analyzeBalance(std::map<worker_id_t, unsigned> &load,
      std::map<worker_id_t, transfer_t> &xfers, unsigned balanceThreshold,
      unsigned minTransfer);
  void analyzeAggregateBalance();
  bool analyzePartitionBalance();

  void displayStatistics();

  void periodicCheck(const boost::system::error_code& error);

public:
  LoadBalancer(boost::asio::io_service &service);
  virtual ~LoadBalancer();

  worker_id_t registerWorker(const std::string &address, int port,
      bool wantsUpdates, bool hasPartitions);
  void deregisterWorker(worker_id_t id);

  void registerProgramParams(const std::string &programName, unsigned crc,
      unsigned statIDCount);
  void checkProgramParams(const std::string &programName, unsigned crc,
      unsigned statIDCount);

  void analyze(worker_id_t id);

  LBTree *getTree() const {
    return tree;
  }

  void updateWorkerStatNodes(worker_id_t id,
      std::vector<LBTree::Node*> &newNodes);
  void updateWorkerStats(worker_id_t id, std::vector<int> &stats);

  void updateCoverageData(worker_id_t id, const cov_update_t &data);

  void updatePartitioningData(worker_id_t id, const part_stat_t &stats);

  Worker* getWorker(worker_id_t id) {
    std::map<worker_id_t, Worker*>::iterator it = workers.find(id);
    if (it == workers.end())
      return NULL;
    else
      return (*it).second;
  }

  int getWorkerCount() {
    return workers.size();
  }

  bool isDone() {
    return done;
  }

  void getAndResetCoverageUpdates(worker_id_t id, cov_update_t &data);

  bool requestAndResetTransfer(worker_id_t id, transfer_t &globalTrans,
      part_transfers_t &partTrans);
};

}

}

#endif /* LOADBALANCER_H_ */
