/*
 * StickyPathStrategy.h
 *
 *  Created on: Apr 28, 2012
 *      Author: bucur
 */

#ifndef STICKYPATHSTRATEGY_H_
#define STICKYPATHSTRATEGY_H_

#include "cloud9/worker/CoreStrategies.h"

namespace cloud9 {

namespace worker {

class StickyPathStrategy: public StateSelectionStrategy {
public:
  StickyPathStrategy();
  virtual ~StickyPathStrategy() { }

  virtual void onStateActivated(SymbolicState *state);
  virtual void onStateDeactivated(SymbolicState *state);

  virtual SymbolicState* onNextStateSelection() {
    return stickyState;
  }

private:
  SymbolicState *stickyState;
  bool started;
};

} /* namespace worker */

} /* namespace cloud9 */

#endif /* STICKYPATHSTRATEGY_H_ */
