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

#ifndef EXECUTIONTRACE_H_
#define EXECUTIONTRACE_H_

#include "klee/StackTrace.h"

#include <vector>
#include <string>
#include <iostream>

namespace klee {
class ExecutionState;
struct KInstruction;
}

namespace cloud9 {

namespace worker {

class ExecutionTraceEntry {
public:
  enum Kind{ Exec, Instr, Break, Control, Debug, Constraint, Event };
  ExecutionTraceEntry() : kind(Exec) {}
  Kind getKind() const{ return kind; }
  virtual ~ExecutionTraceEntry() {}
protected:
  Kind kind;
  ExecutionTraceEntry(Kind k) : kind(k) {}
};

class InstructionTraceEntry: public ExecutionTraceEntry {
private:
  klee::KInstruction *ki;
public:
  InstructionTraceEntry(klee::KInstruction *_ki) : ExecutionTraceEntry(Instr), ki(_ki) {

}

  virtual ~InstructionTraceEntry() {}

  klee::KInstruction *getInstruction() const { return ki; }

  static bool classof(const ExecutionTraceEntry* entry){ return entry->getKind()==Instr; }
};

class BreakpointEntry: public ExecutionTraceEntry {
private:
  unsigned int id;
public:
  BreakpointEntry(unsigned int _id) : ExecutionTraceEntry(Break), id(_id) {

  }

  virtual ~BreakpointEntry() { }

  unsigned int getID() const { return id; }

  static bool classof(const ExecutionTraceEntry* entry){  return entry->getKind()==Break;  }
};

class ControlFlowEntry: public ExecutionTraceEntry {
private:
  bool branch;
  bool call;
  bool fnReturn;
public:
  ControlFlowEntry(bool _branch, bool _call, bool _return) : ExecutionTraceEntry(Control),
    branch(_branch), call(_call), fnReturn(_return) {

  }

  virtual ~ControlFlowEntry() { }

  bool isBranch() const { return branch; }
  bool isCall() const { return call; }
  bool isReturn() const { return fnReturn; }

  static bool classof(const ExecutionTraceEntry* entry){  return entry->getKind()==Control;  }
};

class DebugLogEntry: public ExecutionTraceEntry {
protected:
  std::string message;

  DebugLogEntry() { }
  DebugLogEntry(Kind k) : ExecutionTraceEntry(k) {}
public:
  DebugLogEntry(const std::string &msg) : ExecutionTraceEntry(Debug), message(msg) {

  }

  virtual ~DebugLogEntry() { }

  const std::string &getMessage() const { return message; }

  static bool classof(const ExecutionTraceEntry* entry){  return entry->getKind()==Debug;  }
};

class EventEntry: public ExecutionTraceEntry {
private:
  klee::StackTrace stackTrace;
  unsigned int type;
  long int value;
public:
  EventEntry(klee::StackTrace _stackTrace, unsigned int _type, long int _value) : ExecutionTraceEntry(Event),
    stackTrace(_stackTrace), type(_type), value(_value) {

  }

  const klee::StackTrace &getStackTrace() const { return stackTrace; }
  unsigned int getType() const { return type; }
  long int getValue() const { return value; }

  static bool classof(const ExecutionTraceEntry* entry){  return entry->getKind()==Event;  }
};

class ExecutionTrace {
public:
  typedef std::vector<ExecutionTraceEntry*>::iterator iterator;
  typedef std::vector<ExecutionTraceEntry*>::const_iterator const_iterator;

private:
  std::vector<ExecutionTraceEntry*> entries;
public:
  ExecutionTrace();
  virtual ~ExecutionTrace();

  const std::vector<ExecutionTraceEntry*> &getEntries() const { return entries; }

  void appendEntry(ExecutionTraceEntry *entry) {
    entries.push_back(entry);
  }
};

}

}

#endif /* EXECUTIONTRACE_H_ */
