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

#include "cloud9/ExecutionPath.h"

#include "cloud9/ExecutionTree.h"
#include "cloud9/Protocols.h"

#include <string>
#include <vector>
#include <map>

namespace cloud9 {

ExecutionPath *ExecutionPath::getAbsolutePath() {
  ExecutionPath *absPath = new ExecutionPath();
  absPath->parent = NULL;
  absPath->parentIndex = 0;

  ExecutionPath *crtPath = this;
  int crtIndex = crtPath->path.size();

  while (crtPath != NULL) {
    absPath->path.insert(absPath->path.begin(), crtPath->path.begin(),
        crtPath->path.begin() + crtIndex);

    crtIndex = crtPath->parentIndex;
    crtPath = crtPath->parent;
  }

  return absPath;
}

ExecutionPathPin ExecutionPathSet::getPath(unsigned int index) {
  assert(index < paths.size());

  ExecutionPath *path = paths[index]->getAbsolutePath();

  return ExecutionPathPin(path);
}

ExecutionPathSet::ExecutionPathSet() {
  // Do nothing
}


ExecutionPathSet::~ExecutionPathSet() {
  // Delete the paths
  for (iterator it = paths.begin(); it != paths.end(); it++) {
    delete *it;
  }
}

ExecutionPathSetPin ExecutionPathSet::parse(std::istream &is) {
  ExecutionPathSet *set = new ExecutionPathSet();

  char crtChar = '\0';

  for (;;) {
    // Move to the first valid path character
    while (!is.eof() && crtChar != '0' && crtChar != '1') {
      is.get(crtChar);
    }
    if (is.eof())
      break;

    // Start building the path
    ExecutionPath *path = new ExecutionPath();
    do {
      path->path.push_back(crtChar - '0');
      is.get(crtChar);
    } while (!is.eof() && (crtChar == '0' || crtChar == '1'));

    set->paths.push_back(path);
  }

  return ExecutionPathSetPin(set);
}

}
