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

#include "cloud9/lb/WorkerConnection.h"
#include "cloud9/lb/LoadBalancer.h"
#include "cloud9/lb/Worker.h"
#include "cloud9/lb/LBCommon.h"
#include "cloud9/Logger.h"

#include "llvm/Support/CommandLine.h"

#include <boost/bind.hpp>
#include <string>
#include <vector>
#include <sstream>

using namespace llvm;
using namespace cloud9::data;

namespace {
  cl::opt<bool> DebugWorkerCommunication("debug-worker-communication", cl::init(false));
}

namespace cloud9 {

namespace lb {

WorkerConnection::WorkerConnection(boost::asio::io_service &service,
    LoadBalancer *_lb) :
  socket(service), lb(_lb), worker(NULL), msgReader(socket), msgWriter(socket) {

}

WorkerConnection::~WorkerConnection() {
  if (worker) {
    CLOUD9_WRK_INFO(worker, "Connection interrupted");

    lb->deregisterWorker(worker->getID());
  }

  if (lb->getWorkerCount() == 0) {
    // We should exit the load balancer
    socket.get_io_service().stop();
  }
}

void WorkerConnection::start() {
  msgReader.recvMessage(boost::bind(&WorkerConnection::handleMessageReceived,
      shared_from_this(), _1, _2));
}

void WorkerConnection::handleMessageReceived(std::string &msgString,
    const boost::system::error_code &error) {

  if (error) {
    CLOUD9_WRK_ERROR(worker, "Error receiving message from worker: " << error.message());
    return;
  }

  // Construct the protocol buffer message
  WorkerReportMessage message;

  bool result = message.ParseFromString(msgString);
  assert(result);

  LBResponseMessage response;

  int id = message.id();

  if (id == 0) {
    const WorkerReportMessage_Registration &regInfo = message.registration();

    if (lb->getWorkerCount() == 0) {
      // The first worker sets the program parameters
      lb->registerProgramParams(regInfo.prog_name(), regInfo.prog_crc(),
          regInfo.stat_id_count());
    } else {
      // Validate that the worker executes the same thing as the others
      lb->checkProgramParams(regInfo.prog_name(), regInfo.prog_crc(),
          regInfo.stat_id_count());
    }

    id = lb->registerWorker(regInfo.address(), regInfo.port(),
        regInfo.wants_updates());
    worker = lb->getWorker(id);

    response.set_id(id);
    response.set_more_details(false);
    response.set_terminate(false);

    if (lb->getWorkerCount() == 1) {
      // Send the seed information
      LBResponseMessage_JobSeed *jobSeed = response.mutable_jobseed();
      cloud9::data::ExecutionPathSet *pathSet = jobSeed->mutable_path_set();

      std::vector<LBTree::Node*> nodes;
      nodes.push_back(lb->getTree()->getRoot());

      ExecutionPathSetPin paths = lb->getTree()->buildPathSet(nodes.begin(),
          nodes.end(), (std::map<LBTree::Node*,unsigned>*)NULL);

      serializeExecutionPathSet(paths, *pathSet);
    }
  } else {
    if (lb->getWorker(id) == NULL) { // The worker was timed-out
      worker = NULL;
      CLOUD9_WRK_ERROR(worker, "Message received after time-out");
      return;
    }
    //processNodeSetUpdate(message); // XXX We disable this for now, it's useless
    processNodeDataUpdate(message);
    processStatisticsUpdates(message);

    lb->analyze(id);

    response.set_id(id);
    response.set_more_details(lb->requestAndResetDetails(id));
    response.set_terminate(lb->isDone());

    sendJobTransfers(response);

    if (worker->wantsUpdates())
      sendStatisticsUpdates(response);
  }

  std::string respString;
  result = response.SerializeToString(&respString);
  assert(result);

  msgWriter.sendMessage(respString, boost::bind(&WorkerConnection::handleMessageSent,
      shared_from_this(), _1));

}

void WorkerConnection::sendJobTransfers(LBResponseMessage &response) {
  worker_id_t id = response.id();
  TransferRequest *transfer = lb->requestAndResetTransfer(id);

  if (transfer) {
    const Worker *destination = lb->getWorker(transfer->toID);

    LBResponseMessage_JobTransfer *transMsg = response.mutable_jobtransfer();

    transMsg->set_dest_address(destination->getAddress());
    transMsg->set_dest_port(destination->getPort());

    cloud9::data::ExecutionPathSet *pathSet = transMsg->mutable_path_set();
    serializeExecutionPathSet(transfer->paths, *pathSet);

    for (std::vector<int>::iterator it = transfer->counts.begin(); it
        != transfer->counts.end(); it++) {
      transMsg->add_count(*it);
    }

    delete transfer;
  }
}

void WorkerConnection::sendStatisticsUpdates(LBResponseMessage &response) {
  worker_id_t id = response.id();
  cov_update_t data;

  lb->getAndResetCoverageUpdates(id, data);

  if (data.size() > 0) {
    if (DebugWorkerCommunication) {
      CLOUD9_WRK_DEBUG(worker, "Sent coverage updates: " << covUpdatesToString(data));
    }

    StatisticUpdate *update = response.add_globalupdates();
    serializeStatisticUpdate(CLOUD9_STAT_NAME_GLOBAL_COVERAGE, data, *update);
  }

}

void WorkerConnection::handleMessageSent(const boost::system::error_code &error) {
  if (!error) {
    // Wait for another message, again
    msgReader.recvMessage(boost::bind(&WorkerConnection::handleMessageReceived,
        shared_from_this(), _1, _2));
  } else {
    CLOUD9_WRK_ERROR(worker, "Could not send reply");
  }
}

bool WorkerConnection::processStatisticsUpdates(
    const WorkerReportMessage &message) {
  if (message.localupdates_size() == 0)
    return false;

  worker_id_t id = message.id();

  for (int i = 0; i < message.localupdates_size(); i++) {
    const StatisticUpdate &update = message.localupdates(i);

    if (update.name() == CLOUD9_STAT_NAME_LOCAL_COVERAGE) {
      cov_update_t data;
      parseStatisticUpdate(update, data);

      if (DebugWorkerCommunication) {
        CLOUD9_WRK_DEBUG(worker, "Received coverage updates: " << covUpdatesToString(data));
      }

      if (data.size() > 0)
        lb->updateCoverageData(id, data);
    }
  }

  return true;
}

bool WorkerConnection::processNodeSetUpdate(const WorkerReportMessage &message) {
  if (!message.has_nodesetupdate())
    return false;

  worker_id_t id = message.id();
  const WorkerReportMessage_NodeSetUpdate &nodeSetUpdateMsg =
      message.nodesetupdate();

  std::vector<LBTree::Node*> nodes;
  ExecutionPathSetPin paths = parseExecutionPathSet(nodeSetUpdateMsg.pathset());

  lb->getTree()->getNodes(LB_LAYER_DEFAULT, paths, nodes,
      (std::map<unsigned,LBTree::Node*>*)NULL);

  //CLOUD9_DEBUG("Received node set: " << getASCIINodeSet(nodes.begin(),
  //		nodes.end()));

  lb->updateWorkerStatNodes(id, nodes);

  return true;
}

bool WorkerConnection::processNodeDataUpdate(const WorkerReportMessage &message) {
  if (!message.has_nodedataupdate())
    return false;

  worker_id_t id = message.id();
  const WorkerReportMessage_NodeDataUpdate &nodeDataUpdateMsg =
      message.nodedataupdate();

  std::vector<int> data;

  data.insert(data.begin(), nodeDataUpdateMsg.data().begin(),
      nodeDataUpdateMsg.data().end());

  //CLOUD9_WRK_DEBUG(worker, "Received data set: " << getASCIIDataSet(data.begin(), data.end()));

  lb->updateWorkerStats(id, data);

  return true;
}

}

}
