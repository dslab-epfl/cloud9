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

#include "cloud9/worker/SymbolicEngine.h"
#include "klee/ExecutionState.h"

#include <boost/bind.hpp>
#include <glog/logging.h>

namespace cloud9 {

namespace worker {

// TODO: Use boost functions to remove redundancy in the code

void SymbolicEngine::registerStateEventHandler(StateEventHandler *handler) {
  seHandlers.insert(handler);
}

void SymbolicEngine::deregisterStateEventHandler(StateEventHandler *handler) {
  seHandlers.erase(handler);
}

bool SymbolicEngine::fireStateBranching(klee::ExecutionState *state, klee::ForkTag forkTag) {
  if (seHandlers.size() == 0)
    return false;


  int result = true;

  for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
    StateEventHandler *h = *it;

    result = result && h->onStateBranching(state, forkTag);
  }

  return result;
}

void SymbolicEngine::fireStateBranched(klee::ExecutionState *state,
    klee::ExecutionState *parent, int index, klee::ForkTag forkTag) {

  fireHandler(boost::bind(&StateEventHandler::onStateBranched, _1, state, parent, index, forkTag));
}

void SymbolicEngine::fireStateDestroy(klee::ExecutionState *state, bool silenced) {

  for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
    StateEventHandler *h = *it;

    h->onStateDestroy(state, silenced);
  }
}

void SymbolicEngine::fireControlFlowEvent(klee::ExecutionState *state,
      ControlFlowEvent event) {

  for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
    StateEventHandler *h = *it;

    h->onControlFlowEvent(state, event);
  }
}

void SymbolicEngine::fireDebugInfo(klee::ExecutionState *state,
    const std::string &message) {

  for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
    StateEventHandler *h = *it;

    h->onDebugInfo(state, message);
  }
}

void SymbolicEngine::fireOutOfResources(klee::ExecutionState *destroyedState) {

  for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
    StateEventHandler *h = *it;

    h->onOutOfResources(destroyedState);
  }
}

void SymbolicEngine::fireEvent(klee::ExecutionState *state, unsigned int type,
    long int value) {

  fireHandler(boost::bind(&StateEventHandler::onEvent, _1, state, type, value));
}


}

}
