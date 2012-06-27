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

#ifndef PEERSERVER_H_
#define PEERSERVER_H_

#include "cloud9/Protocols.h"

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::asio::ip;

namespace cloud9 {

namespace worker {

class JobManager;

class PeerConnection: public boost::enable_shared_from_this<PeerConnection> {
private:
  tcp::socket socket;
  JobManager *jobManager;
  AsyncMessageReader msgReader;

  void handleMessageReceived(std::string &message,
      const boost::system::error_code &error);

  PeerConnection(boost::asio::io_service& service, JobManager *jobManager);
public:
  typedef boost::shared_ptr<PeerConnection> pointer;

  static pointer create(boost::asio::io_service& service, JobManager *jobManager) {
    return pointer(new PeerConnection(service, jobManager));
  }

  void start();

  tcp::socket &getSocket() { return socket; }
};

class PeerServer {

private:
  tcp::acceptor acceptor;

  JobManager *jobManager;

  void startAccept();

  void handleAccept(PeerConnection::pointer socket, const boost::system::error_code &error);
public:
  PeerServer(boost::asio::io_service &service, JobManager *jobManager);
  virtual ~PeerServer();
};

}

}

#endif /* PEERSERVER_H_ */
