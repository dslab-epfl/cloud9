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

#ifndef EXECUTIONPATH_H_
#define EXECUTIONPATH_H_

#include <vector>
#include <iostream>
#include <boost/shared_ptr.hpp>

namespace cloud9 {

namespace data {
class ExecutionPathSet;
}

class ExecutionPath;
class ExecutionPathSet;

typedef boost::shared_ptr<ExecutionPath> ExecutionPathPin;
typedef boost::shared_ptr<ExecutionPathSet> ExecutionPathSetPin;

class ExecutionPath {
  template<class, int, int>
  friend class ExecutionTree;
  friend class ExecutionPathSet;

  friend ExecutionPathSetPin parseExecutionPathSet(const cloud9::data::ExecutionPathSet &ps);

  friend void serializeExecutionPathSet(ExecutionPathSetPin &set,
      cloud9::data::ExecutionPathSet &result);
private:
  std::vector<int> path;

  ExecutionPath *parent;
  int parentIndex;

  ExecutionPath *getAbsolutePath();

  ExecutionPath() : parent(NULL), parentIndex(0) { };
public:
  virtual ~ExecutionPath() { };

  const std::vector<int> &getPath() const {
    assert(parent == NULL);
    return path;
  };

  typedef std::vector<int>::iterator path_iterator;
};


class ExecutionPathSet {
  template<class, int, int>
  friend class ExecutionTree;

  friend ExecutionPathSetPin parseExecutionPathSet(const cloud9::data::ExecutionPathSet &ps);

  friend void serializeExecutionPathSet(ExecutionPathSetPin &set,
      cloud9::data::ExecutionPathSet &result);
private:
  std::vector<ExecutionPath*> paths;

  ExecutionPathSet();

  typedef std::vector<ExecutionPath*>::iterator iterator;
public:
  virtual ~ExecutionPathSet();

  unsigned count() const {
    return paths.size();
  }

  ExecutionPathPin getPath(unsigned int index);

  static ExecutionPathSetPin getEmptySet() {
    return ExecutionPathSetPin(new ExecutionPathSet());
  }

  static ExecutionPathSetPin getRootSet() {
    ExecutionPathSet *pSet = new ExecutionPathSet();
    pSet->paths.push_back(new ExecutionPath());

    return ExecutionPathSetPin(pSet);
  }

  static ExecutionPathSetPin parse(std::istream &is);
};


}


#endif /* EXECUTIONPATH_H_ */
