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

#ifndef WORKERCONNECTION_H_
#define WORKERCONNECTION_H_

#include "cloud9/Protocols.h"

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::asio::ip;
using namespace cloud9::data;

namespace cloud9 {

namespace lb {

class LBServer;
class Worker;
class LoadBalancer;

class WorkerConnection: public boost::enable_shared_from_this<WorkerConnection> {
private:
  tcp::socket socket;

  LoadBalancer *lb;

  Worker *worker;

  AsyncMessageReader msgReader;
  AsyncMessageWriter msgWriter;

  WorkerConnection(boost::asio::io_service &service, LoadBalancer *lb);

  void handleMessageReceived(std::string &message,
      const boost::system::error_code &error);

  void handleMessageSent(const boost::system::error_code &error);

  bool processNodeSetUpdate(const WorkerReportMessage &message);
  bool processNodeDataUpdate(const WorkerReportMessage &message);
  bool processStatisticsUpdates(const WorkerReportMessage &message);
  bool processPartitionUpdates(const WorkerReportMessage &message);

  void sendJobTransfers(LBResponseMessage &response);
  void sendStatisticsUpdates(LBResponseMessage &response);

public:
  typedef boost::shared_ptr<WorkerConnection> pointer;

  static pointer create(boost::asio::io_service &service, LoadBalancer *lb) {
    return pointer(new WorkerConnection(service, lb));
  }

  void start();

  virtual ~WorkerConnection();

  tcp::socket &getSocket() {
    return socket;
  }
};

}

}

#endif /* WORKERCONNECTION_H_ */
