//===-- CoreStats.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/CoreStats.h"

using namespace klee;

Statistic stats::allocations("Allocations", "Alloc");
Statistic stats::locallyCoveredInstructions("LocallyCoveredInstructions", "LIcov");
Statistic stats::globallyCoveredInstructions("GloballyCoveredInstructions", "GIcov");
Statistic stats::falseBranches("FalseBranches", "Bf");
Statistic stats::forkTime("ForkTime", "Ftime");
Statistic stats::forks("Forks", "Forks");
Statistic stats::instructionRealTime("InstructionRealTimes", "Ireal");
Statistic stats::instructionTime("InstructionTimes", "Itime");
Statistic stats::instructions("Instructions", "I");
Statistic stats::minDistToReturn("MinDistToReturn", "Rdist");
Statistic stats::minDistToGloballyUncovered("MinDistToGloballyUncovered", "UCdist");
Statistic stats::reachableGloballyUncovered("ReachableGloballyUncovered", "IuncovReach");
Statistic stats::resolveTime("ResolveTime", "Rtime");
Statistic stats::solverTime("SolverTime", "Stime");
Statistic stats::states("States", "States");
Statistic stats::trueBranches("TrueBranches", "Bt");
Statistic stats::locallyUncoveredInstructions("LocallyUncoveredInstructions", "LIuncov");
Statistic stats::globallyUncoveredInstructions("GloballyUncoveredInstructions", "GIuncov");
