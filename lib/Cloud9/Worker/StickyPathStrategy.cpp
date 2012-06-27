/*
 * StickyPathStrategy.cpp
 *
 *  Created on: Apr 28, 2012
 *      Author: bucur
 */

#include "cloud9/worker/StickyPathStrategy.h"

#include "klee/Internal/ADT/RNG.h"
#include "cloud9/worker/WorkerCommon.h"

#include <time.h>

using namespace klee;

namespace cloud9 {

namespace worker {

StickyPathStrategy::StickyPathStrategy()
  : stickyState(NULL), started(false) {
  theRNG.seed(::time(NULL));
}

void StickyPathStrategy::onStateActivated(SymbolicState *state) {
  if (!started || theRNG.getBool()) {
    stickyState = state;
    started = true;
  }
}

void StickyPathStrategy::onStateDeactivated(SymbolicState *state) {
  if (state == stickyState) {
    stickyState = NULL;
  }
}

} /* namespace worker */

} /* namespace cloud9 */
