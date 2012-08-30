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

#include "cloud9/worker/PeerServer.h"
#include "cloud9/worker/WorkerCommon.h"
#include "cloud9/worker/JobManager.h"
#include "cloud9/Protocols.h"

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <vector>

#include <glog/logging.h>

using namespace cloud9::data;

namespace cloud9 {

namespace worker {

PeerConnection::PeerConnection(boost::asio::io_service& service,
    JobManager *jm) :
  socket(service), jobManager(jm),
  msgReader(socket) {

}

void PeerConnection::start() {
  // All we do is to read the job transfer request
  msgReader.recvMessage(boost::bind(&PeerConnection::handleMessageReceived,
        shared_from_this(), _1, _2));
}

void PeerConnection::handleMessageReceived(std::string &msgString,
    const boost::system::error_code &error) {
  if (!error) {
    // Decode the message and apply the changes
    PeerTransferMessage message;
    message.ParseFromString(msgString);

    const cloud9::data::ExecutionPathSet &pathSet = message.path_set();

    ExecutionPathSetPin paths = parseExecutionPathSet(pathSet);
    std::vector<long> replayInstrs;

    for (int i = 0; i < message.instr_since_fork_size(); i++) {
      replayInstrs.push_back(message.instr_since_fork(i));
    }

    jobManager->importJobs(paths, replayInstrs);
  } else {
    LOG(ERROR) << "Error receiving message from peer";
  }
}

PeerServer::PeerServer(boost::asio::io_service &service, JobManager *jm) :
  acceptor(service, tcp::endpoint(tcp::v4(), LocalPort)), jobManager(jm) {

  startAccept();
}

PeerServer::~PeerServer() {
  // TODO Auto-generated destructor stub
}

void PeerServer::startAccept() {
  LOG(INFO) << "Listening for peer connections on port " <<
      acceptor.local_endpoint().port();


  PeerConnection::pointer newConn = PeerConnection::create(acceptor.get_io_service(),
      jobManager);

  acceptor.async_accept(newConn->getSocket(), boost::bind(&PeerServer::handleAccept,
      this, newConn, boost::asio::placeholders::error));
}

void PeerServer::handleAccept(PeerConnection::pointer conn,
    const boost::system::error_code &error) {

  if (!error) {
    conn->start();
    // Go back accepting other connections
    startAccept();
  } else {
    LOG(ERROR) << "Error accepting peer connection";
  }


}

}

}
