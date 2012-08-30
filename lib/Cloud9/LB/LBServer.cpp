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

#include "cloud9/lb/LBServer.h"
#include "cloud9/lb/WorkerConnection.h"

#include <boost/bind.hpp>

#include <glog/logging.h>

namespace cloud9 {

namespace lb {

LBServer::LBServer(LoadBalancer *_lb, boost::asio::io_service &io_service,
    int port) :
  acceptor(io_service, tcp::endpoint(tcp::v4(), port)), lb(_lb) {

  startAccept();
}

LBServer::~LBServer() {
  // TODO Auto-generated destructor stub
}

void LBServer::startAccept() {
  WorkerConnection::pointer conn = WorkerConnection::create(
      acceptor.get_io_service(), lb);

  LOG(INFO) << "Listening for connections on port " <<
      acceptor.local_endpoint().port();

  acceptor.async_accept(conn->getSocket(), boost::bind(&LBServer::handleAccept,
      this, conn, boost::asio::placeholders::error));

}

void LBServer::handleAccept(WorkerConnection::pointer conn,
    const boost::system::error_code &error) {
  if (!error) {
    LOG(INFO) << "Connection received from " << conn->getSocket().remote_endpoint().address();

    conn->start();

    // Go back and accept another connection
    startAccept();
  } else {
    LOG(ERROR) << "Error accepting worker connection";
  }

}

}

}
