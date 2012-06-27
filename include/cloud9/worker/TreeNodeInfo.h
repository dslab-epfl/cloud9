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

#ifndef TREENODEINFO_H_
#define TREENODEINFO_H_

#include "cloud9/ExecutionTree.h"
#include "cloud9/worker/ExecutionTrace.h"
#include "klee/ForkTag.h"

#include <vector>

namespace klee {
class ExecutionState;
}

namespace cloud9 {

namespace worker {

class ExecutionTrace;
class SymbolicState;
class ExecutionJob;

class WorkerNodeInfo {
  friend class JobManager;
  friend class SymbolicState;
  friend class ExecutionJob;
private:
  SymbolicState *symState;
  ExecutionJob *job;
  ExecutionTrace trace;
  klee::ForkTag forkTag;
public:
  WorkerNodeInfo() : symState(NULL), job(NULL), forkTag(klee::KLEE_FORK_DEFAULT) { }

  virtual ~WorkerNodeInfo() { }

  SymbolicState* getSymbolicState() const { return symState; }
  ExecutionJob* getJob() const { return job; }
  klee::ForkTag getForkTag() const { return forkTag; }

  const ExecutionTrace &getTrace() const { return trace; }
};

#define WORKER_LAYER_COUNT      5

#define WORKER_LAYER_JOBS     1
#define WORKER_LAYER_STATES     2
#define WORKER_LAYER_STATISTICS   3
#define WORKER_LAYER_BREAKPOINTS  4

typedef ExecutionTree<WorkerNodeInfo, WORKER_LAYER_COUNT, 2> WorkerTree; // Four layered, binary tree

class WorkerNodeDecorator: public DotNodeDefaultDecorator<WorkerTree::Node> {
public:
  WorkerNodeDecorator(WorkerTree::Node *highlight) :
    DotNodeDefaultDecorator<WorkerTree::Node>(WORKER_LAYER_STATES, WORKER_LAYER_JOBS, highlight) {

  }

  void operator() (WorkerTree::Node *node, deco_t &deco, edge_deco_t &inEdges) {
    DotNodeDefaultDecorator<WorkerTree::Node>::operator() (node, deco, inEdges);

    bool zombie = !node->layerExists(WORKER_LAYER_STATES) && !node->layerExists(WORKER_LAYER_JOBS);

    if (zombie) {
      deco["color"] = "gray25";
    }

    WorkerTree::Node *parent = node->getParent();
    if (parent) {
      deco_t edeco;
      edeco["label"] = node->getIndex() ? "1" : "0";
      if (zombie) {
        edeco["color"] = "gray25";
      }
      inEdges.push_back(std::make_pair(parent, edeco));
    }
  }
};

class LayerHighlightDecorator: public WorkerNodeDecorator {
private:
  int layer;
public:
  LayerHighlightDecorator(int _layer) :
      WorkerNodeDecorator(NULL), layer(_layer) { }

  void operator() (WorkerTree::Node *node, deco_t &deco, edge_deco_t &inEdges) {
    WorkerNodeDecorator::operator() (node, deco, inEdges);

    if (node->layerExists(layer)) {
      deco["fillcolor"] = "red";
    }
  }
};

}

}

#endif /* TREENODEINFO_H_ */
