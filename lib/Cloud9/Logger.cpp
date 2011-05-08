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

#include "cloud9/Common.h"
#include "cloud9/Logger.h"

#ifdef CLOUD9_HAVE_ADVANCED_LOGGING
#include "Log4CXXLogger.h"
#endif
#include "SimpleLogger.h"

#include "llvm/ADT/STLExtras.h"

#include <dlfcn.h>
#include <cxxabi.h>
#include <execinfo.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace llvm;

namespace cloud9 {

#ifdef CLOUD9_HAVE_ADVANCED_LOGGING
	Logger Logger::logger = Log4CXXLogger();
#else
	Logger Logger::logger = SimpleLogger();
#endif

static std::string GetStackTrace(int startingFrom = 0, int maxDepth = 4) {
	static void* StackTrace[256];
	static char lineBuffer[256];
	std::string result;

	// Use backtrace() to output a backtrace on Linux systems with glibc.
	int depth = backtrace(StackTrace, static_cast<int> (array_lengthof(
			StackTrace)));

	if (depth > startingFrom + maxDepth)
		depth = startingFrom + maxDepth;

	for (int i = startingFrom; i < depth; ++i) {
		if (i > startingFrom)
			result.append(" ");
		Dl_info dlinfo;
		dladdr(StackTrace[i], &dlinfo);
		//const char* name = strrchr(dlinfo.dli_fname, '/');

		snprintf(lineBuffer, 256, "%-3d", i);

		result.append(lineBuffer);

		if (dlinfo.dli_sname != NULL) {
			int res;
			char* d = abi::__cxa_demangle(dlinfo.dli_sname, NULL, NULL, &res);

			snprintf(lineBuffer, 256, " %s + %ld",
					(d == NULL) ? dlinfo.dli_sname : d, (char*) StackTrace[i]
							- (char*) dlinfo.dli_saddr);
			result.append(lineBuffer);

			free(d);
		}
	}

	return result;
}

void Logger::updateTimeStamp() {
	llvm::sys::TimeValue elapsed = llvm::sys::TimeValue::now() - startTime;
	char buff[16];

	snprintf(buff, 16, "[%05ld.%03d] ", elapsed.seconds(), elapsed.milliseconds());

	timeStamp.assign(buff);
}

Logger &Logger::getLogger() {
	return Logger::logger;
}

std::string Logger::getStackTrace() {
	return cloud9::GetStackTrace(2, 8);
}

}

std::string operator*(const std::string input, unsigned int n) {
  std::string result;

  for (unsigned int i = 0; i < n; i++) {
    result.append(input);
  }

  return result;
}
