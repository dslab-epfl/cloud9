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

#include "cloud9/worker/WorkerCommon.h"

#include "llvm/Support/CommandLine.h"

#include <cstdlib>
#include <string>


using namespace llvm;

std::string InputFile;
LibcType Libc;
bool WithPOSIXRuntime;

bool UseGlobalCoverage;

std::string LBAddress;
int LBPort;

std::string LocalAddress;
int LocalPort;

int RetryConnectTime;
int UpdateTime;

namespace {
static cl::opt<std::string, true> InputFileOpt(cl::desc("<input bytecode>"), cl::Positional,
    cl::location(InputFile), cl::init("-"));

static cl::opt<LibcType, true> LibcOpt("libc", cl::desc(
    "Choose libc version (none by default)."), cl::values(
    clEnumValN(NoLibc, "none", "Don't link in a libc"),
      clEnumValN(UcLibc, "uclibc", "Link in uclibc (adapted for klee)"),
      clEnumValEnd) , cl::location(Libc), cl::init(NoLibc));

static cl::opt<bool, true> WithPOSIXRuntimeOpt("posix-runtime", cl::desc(
    "Link with POSIX runtime"), cl::location(WithPOSIXRuntime), cl::init(false));

static cl::opt<bool, true> UseGlobalCoverageOpt("c9-use-global-cov",
    cl::desc("Use global coverage information in the searcher"),
    cl::location(UseGlobalCoverage), cl::init(true));

static cl::opt<std::string, true> LBAddressOpt("c9-lb-host",
    cl::desc("Host name of the load balancer"),
    cl::location(LBAddress), cl::init("localhost"));

static cl::opt<int, true> LBPortOpt("c9-lb-port",
    cl::desc("Port number of the load balancer"),
    cl::location(LBPort), cl::init(1337));


static cl::opt<std::string, true> LocalAddressOpt("c9-local-host",
    cl::desc("Host name of the local peer server"),
    cl::location(LocalAddress), cl::init("localhost"));

static cl::opt<int, true> LocalPortOpt("c9-local-port",
    cl::desc("Port number of the local peer server"),
    cl::location(LocalPort), cl::init(1234));

static cl::opt<int, true> RetryConnectTimeOpt("c9-lb-connect-retry",
    cl::desc("The time in seconds after the next connection retry"),
    cl::location(RetryConnectTime), cl::init(2));

static cl::opt<int, true> UpdateTimeOpt("c9-lb-update",
    cl::desc("The time in seconds between load balancing updates"),
    cl::location(UpdateTime), cl::init(5));

}
