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

#ifndef TIMING_H_
#define TIMING_H_

#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include <ostream>
#include <cassert>

namespace cloud9 {

namespace instrum {

class Timer {
private:
  struct timeval startThreadTime;
  struct timeval startRealTime;

  struct timeval endThreadTime;
  struct timeval endRealTime;

  double getTime(const struct timeval &start, const struct timeval &end) const {
    struct timeval res;
    timersub(&end, &start, &res);

    return ((double)res.tv_sec + (double)res.tv_usec/1000000.0);
  }

  void convert(struct timeval &dst, struct timespec &src) {
    dst.tv_sec = src.tv_sec;
    dst.tv_usec = src.tv_nsec / 1000;
  }

public:
  Timer() { }
  ~Timer() { }

#if 0
  void start() {
    int res = gettimeofday(&startRealTime, 0);
    assert(res == 0);

    struct rusage ru;
    res = getrusage(RUSAGE_THREAD, &ru);
    assert(res == 0);

    timeradd(&ru.ru_utime, &ru.ru_stime, &startThreadTime);
  }

  void stop() {
    int res = gettimeofday(&endRealTime, 0);
    assert(res == 0);

    struct rusage ru;
    res = getrusage(RUSAGE_THREAD, &ru);
    assert(res == 0);

    timeradd(&ru.ru_utime, &ru.ru_stime, &endThreadTime);
  }
#else
  void start() {
    struct timespec tp;

    int res = clock_gettime(CLOCK_REALTIME, &tp);
    assert(res == 0);

    convert(startRealTime, tp);

    res = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
    assert(res == 0);

    convert(startThreadTime, tp);
  }

  void stop() {
    struct timespec tp;

    int res = clock_gettime(CLOCK_REALTIME, &tp);
    assert(res == 0);

    convert(endRealTime, tp);

    res = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
    assert(res == 0);

    convert(endThreadTime, tp);
  }
#endif

  double getThreadTime() const { return getTime(startThreadTime, endThreadTime); }
  double getRealTime() const { return getTime(startRealTime, endRealTime); }
};

static inline std::ostream &operator<<(std::ostream &os, const Timer &timer) {
  os << "Thread: " << timer.getThreadTime() <<
       " Real: " << timer.getRealTime();

  return os;
}

}

}


#endif /* TIMING_H_ */
