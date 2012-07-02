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

#ifndef SYMBOLICENGINE_H_
#define SYMBOLICENGINE_H_

#include "klee/ForkTag.h"

#include <set>
#include <string>

namespace klee {
class ExecutionState;
class Searcher;
class KModule;
class Interpreter;
}

namespace llvm {
class Function;
}

namespace cloud9 {

namespace worker {

enum ControlFlowEvent {
  STEP,
  CHANGE_SHADOW_STATE,
  BRANCH_FALSE,
  BRANCH_TRUE,
  CALL,
  RETURN
};

class StateEventHandler {
public:
  StateEventHandler() {};
  virtual ~StateEventHandler() {};

public:
  virtual bool onStateBranching(klee::ExecutionState *state, klee::ForkTag forkTag) = 0;
  virtual void onStateBranched(klee::ExecutionState *state,
      klee::ExecutionState *parent, int index, klee::ForkTag forkTag) = 0;
  virtual void onStateDestroy(klee::ExecutionState *state, bool silenced) = 0;
  virtual void onControlFlowEvent(klee::ExecutionState *state,
      ControlFlowEvent event) = 0;
  virtual void onDebugInfo(klee::ExecutionState *state,
      const std::string &message) = 0;
  virtual void onEvent(klee::ExecutionState *state,
      unsigned int type, long int value) = 0;
  virtual void onOutOfResources(klee::ExecutionState *destroyedState) = 0;

};

class SymbolicEngine {
private:
  typedef std::set<StateEventHandler*> handlers_t;
  handlers_t seHandlers;

  template <class Handler>
  void fireHandler(Handler handler) {
    for (handlers_t::iterator it = seHandlers.begin(); it != seHandlers.end(); it++) {
        StateEventHandler *h = *it;

        handler(h);
      }
  }
protected:
  bool fireStateBranching(klee::ExecutionState *state, klee::ForkTag forkTag);
  void fireStateBranched(klee::ExecutionState *state,
      klee::ExecutionState *parent, int index, klee::ForkTag forkTag);
  void fireStateDestroy(klee::ExecutionState *state, bool silenced);
  void fireControlFlowEvent(klee::ExecutionState *state,
      ControlFlowEvent event);
  void fireDebugInfo(klee::ExecutionState *state, const std::string &message);
  void fireOutOfResources(klee::ExecutionState *destroyedState);
  void fireEvent(klee::ExecutionState *state, unsigned int type, long int value);
public:
  SymbolicEngine() {};
  virtual ~SymbolicEngine() {};

  virtual klee::ExecutionState *createRootState(llvm::Function *f) = 0;
  virtual void initRootState(klee::ExecutionState *state, int argc,
      char **argv, char **envp) = 0;

  virtual void stepInState(klee::ExecutionState *state) = 0;

  virtual void destroyState(klee::ExecutionState *state) = 0;

  virtual void destroyStates() = 0;

  virtual klee::Searcher *initSearcher(klee::Searcher *base) = 0;

  virtual klee::KModule *getModule() = 0;

  void registerStateEventHandler(StateEventHandler *handler);
  void deregisterStateEventHandler(StateEventHandler *handler);

  virtual void PrintDump(std::ostream &os) = 0;

  static bool classof(const klee::Interpreter* interpreter){ return true; }
};

}

}

#endif /* SYMBOLICENGINE_H_ */
