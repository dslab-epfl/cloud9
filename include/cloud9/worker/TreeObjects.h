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

#ifndef TREEOBJECTS_H_
#define TREEOBJECTS_H_

#include "cloud9/worker/TreeNodeInfo.h"
#include "klee/ExecutionState.h"
#include "klee/ForkTag.h"

#include <ostream>

namespace klee {
class KInstruction;
}

namespace cloud9 {

namespace worker {

/*
 *
 */
class SymbolicState {
	friend class JobManager;
	friend class OracleStrategy;
private:
	klee::ExecutionState *kleeState;
	WorkerTree::NodePin nodePin;

	bool _active;

	std::vector<klee::KInstruction*> _instrProgress;
	unsigned int _instrPos;
	unsigned long _instrSinceFork;

	bool collectProgress;

	void rebindToNode(WorkerTree::Node *node) {
		if (nodePin) {
			(**nodePin).symState = NULL;
		}

		if (node) {
			nodePin = node->pin(WORKER_LAYER_STATES);
			(**node).symState = this;
		} else {
			nodePin.reset();
		}
	}

public:
	SymbolicState(klee::ExecutionState *state) :
		kleeState(state),
		nodePin(WORKER_LAYER_STATES),
		_active(false),
		_instrPos(0),
		_instrSinceFork(0),
		collectProgress(false) {
      kleeState->setCloud9State(this);
	}

	virtual ~SymbolicState() {
		rebindToNode(NULL);
	}

	klee::ExecutionState *getKleeState() const { return kleeState; }

	WorkerTree::NodePin &getNode() { return nodePin; }

    klee::ExecutionState& operator*() {
      return *kleeState;
    }

    const klee::ExecutionState& operator*() const {
      return *kleeState;
    }
};

/*
 *
 */
class ExecutionJob {
	friend class JobManager;
private:
	WorkerTree::NodePin nodePin;

	bool imported;
	bool exported;
	bool removing;

	long replayInstr;

	klee::ForkTag forkTag;

    void rebindToNode(WorkerTree::Node *node) {
        if (nodePin) {
            (**nodePin).job = NULL;
        }

        if (node) {
            nodePin = node->pin(WORKER_LAYER_JOBS);
            (**node).job = this;
        } else {
            nodePin.reset();
        }
    }
public:
	ExecutionJob() : nodePin(WORKER_LAYER_JOBS), imported(false),
		exported(false), removing(false), replayInstr(0),
		forkTag(klee::KLEE_FORK_DEFAULT) {}

	ExecutionJob(WorkerTree::Node *node, bool _imported) :
		nodePin(WORKER_LAYER_JOBS), imported(_imported), exported(false),
		removing(false), replayInstr(0), forkTag(klee::KLEE_FORK_DEFAULT) {

		rebindToNode(node);

		//CLOUD9_DEBUG("Created job at " << *node);
	}

	virtual ~ExecutionJob() {
		//CLOUD9_DEBUG("Destroyed job at " << nodePin);
		rebindToNode(NULL);
	}

	WorkerTree::NodePin &getNode() { return nodePin; }

	klee::ForkTag getForkTag() const { return forkTag; }

	bool isImported() const { return imported; }
	bool isExported() const { return exported; }
	bool isRemoving() const { return removing; }
};


std::ostream &operator<< (std::ostream &os, const SymbolicState &state);

}
}


#endif /* TREEOBJECTS_H_ */
