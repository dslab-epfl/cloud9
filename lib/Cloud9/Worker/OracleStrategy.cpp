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

#include "cloud9/worker/OracleStrategy.h"
#include "cloud9/worker/TreeObjects.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Instruction.h"

#include <glog/logging.h>

namespace cloud9 {

namespace worker {

OracleStrategy::OracleStrategy(WorkerTree *_tree, std::vector<unsigned int> &_goalPath,
    JobManager *_jobManager) :
  BasicStrategy(_jobManager), goalPath(_goalPath), tree(_tree) {

}

OracleStrategy::~OracleStrategy() {
  // TODO Auto-generated destructor stub
}

bool OracleStrategy::checkProgress(SymbolicState *state) {
  unsigned int i = 0;
  bool result = true; // Assume by default that everything is OK

  while (state->_instrPos < goalPath.size() && i < state->_instrProgress.size()) {
    unsigned int expected = goalPath[state->_instrPos];
    unsigned int obtained = state->_instrProgress[i]->inst->getOpcode();

    if (expected != obtained) {
      LOG(INFO) << "Oracle instruction mismatch. Expected " <<
          expected << " and got " <<
          obtained << " instead. Instruction: " << state->_instrProgress[i]->info->assemblyLine;

      result = false;
      break;
    }
    state->_instrPos++;
    i++;
  }

  state->_instrProgress.erase(state->_instrProgress.begin(),
      state->_instrProgress.begin() + i);

  return result;
}

ExecutionJob* OracleStrategy::onNextJobSelection() {
  if (validStates.empty())
    return NULL;

  SymbolicState *state = *(validStates.begin());

  return selectJob(tree, state);
}

void OracleStrategy::onStateActivated(SymbolicState *state) {
  if (checkProgress(state)) {
    LOG(INFO) << "Valid state added to the oracle, at instr position " << state->_instrPos;
    validStates.insert(state);
  }
}

void OracleStrategy::onStateUpdated(SymbolicState *state, WorkerTree::Node *oldNode) {
  if (!checkProgress(state)) {
    LOG(INFO) << "State became invalid on the oracle, at instr position " << state->_instrPos;
    // Filter out the states that went off the road
    validStates.erase(state);
  }
}

void OracleStrategy::onStateDeactivated(SymbolicState *state) {
  validStates.erase(state);
}

}

}

