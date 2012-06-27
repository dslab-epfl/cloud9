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

#ifndef LBCOMMON_H_
#define LBCOMMON_H_

#include "cloud9/lb/Worker.h"

#include <string>
#include <cstdio>

namespace cloud9 {

namespace lb {

static std::string getIDLabel(unsigned int id) {
  char prefix[] = "Worker<   >: ";
  if (id > 0)
    sprintf(prefix, "Worker<%03d>: ", id);

  return std::string(prefix);
}

static std::string getWorkerLabel(const Worker *w) {
  if (!w)
    return getIDLabel(0);
  else
    return getIDLabel(w->getID());
}

#define CLOUD9_ID_ERROR(id, msg)   CLOUD9_ERROR(cloud9::lb::getIDLabel(id) << msg)
#define CLOUD9_ID_INFO(id, msg)    CLOUD9_INFO(cloud9::lb::getIDLabel(id) << msg)
#define CLOUD9_ID_DEBUG(id, msg)   CLOUD9_DEBUG(cloud9::lb::getIDLabel(id) << msg)

#define CLOUD9_WRK_ERROR(w, msg)   CLOUD9_ERROR(cloud9::lb::getWorkerLabel(w) << msg)
#define CLOUD9_WRK_INFO(w, msg)    CLOUD9_INFO(cloud9::lb::getWorkerLabel(w) << msg)
#define CLOUD9_WRK_DEBUG(w, msg)   CLOUD9_DEBUG(cloud9::lb::getWorkerLabel(w) << msg)

}

}


#endif /* LBCOMMON_H_ */
