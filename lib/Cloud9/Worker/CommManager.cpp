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

#include "cloud9/worker/CommManager.h"
#include "cloud9/worker/WorkerCommon.h"

#include "cloud9/worker/PeerServer.h"
#include "cloud9/worker/LBConnection.h"

#include <boost/asio.hpp>

#include <glog/logging.h>

namespace cloud9 {

namespace worker {

void CommManager::peerCommunicationControl() {
  PeerServer peerServer(peerCommService, jobManager);

  peerCommService.run();
}

void CommManager::lbCommunicationControl() {
  boost::system::error_code error;

  LOG(INFO) << "Connecting to the load balancer...";
  LBConnection lbConnection(lbCommService, jobManager);

  while (!terminated) {
    lbConnection.connect(error);

    if (error) {
      LOG(ERROR) << "Could not connect to the load balancer: " <<
          error.message() << " Retrying in " << RetryConnectTime << " seconds";

      boost::asio::deadline_timer t(lbCommService, boost::posix_time::seconds(RetryConnectTime));
      t.wait();
      continue;
    }

    break;
  }

  if (terminated)
    return;

  LOG(INFO) << "Connected to the load balancer";

  //try {
    LOG(INFO) << "Registering worker with the load balancer...";
    lbConnection.registerWorker();

    boost::asio::deadline_timer t(lbCommService, boost::posix_time::seconds(UpdateTime));

    while (!terminated) {
      t.wait();
      t.expires_at(t.expires_at() + boost::posix_time::seconds(UpdateTime));
      // XXX If the worker is blocked (e.g. debugging), take care not to
      // trigger the timer multiple times - use expires_from_now() to
      // check this out.

      lbConnection.sendUpdates();
    }

  //} catch (boost::system::system_error &) {
    // Silently ignore
  //}
}

CommManager::CommManager(JobManager *jm) : jobManager(jm), terminated(false) {

}

CommManager::~CommManager() {

}

void CommManager::setup() {
  peerCommThread = boost::thread(&CommManager::peerCommunicationControl, this);
  lbCommThread = boost::thread(&CommManager::lbCommunicationControl, this);
}

void CommManager::finalize() {
  terminated = true;

  if (peerCommThread.joinable()) {
    peerCommService.stop();
    peerCommThread.join();
  }

  if (lbCommThread.joinable()) {
    lbCommService.stop();
    lbCommThread.join();
  }
}

}

}
