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

#ifndef WORKER_H_
#define WORKER_H_

#include "cloud9/lb/TreeNodeInfo.h"

#include <vector>
#include <map>

namespace cloud9 {

namespace lb {

typedef unsigned int worker_id_t;

class Worker {
  friend class LoadBalancer;
public:
  struct IDCompare {
    bool operator()(const Worker *a, const Worker *b) {
      return a->getID() < b->getID();
    }
  };

  struct LoadCompare {
    bool operator()(const Worker *a, const Worker *b) {
      return a->getTotalJobs() < b->getTotalJobs();
    }
  };
private:
  worker_id_t id;
  std::string address;
  int port;

  bool _wantsUpdates;

  std::vector<LBTree::Node*> nodes;
  unsigned nodesRevision;

  std::vector<char> globalCoverageUpdates;

  unsigned int totalJobs;

  std::set<std::string> interests;
  bool updatedInterests;

  std::map<std::string, unsigned int> jobsBreakdown;


  unsigned int lastReportTime;

  Worker() : nodesRevision(1), totalJobs(0), updatedInterests(false), lastReportTime(0) {

  }
public:
  virtual ~Worker() {
  }

  worker_id_t getID() const {
    return id;
  }

  unsigned getTotalJobs() const {
    return totalJobs;
  }

  const std::string &getAddress() const {
    return address;
  }
  int getPort() const {
    return port;
  }

  bool operator<(const Worker& w) {
    return id < w.id;
  }

  bool wantsUpdates() const {
    return _wantsUpdates;
  }
};

}

}

#endif /* WORKER_H_ */
