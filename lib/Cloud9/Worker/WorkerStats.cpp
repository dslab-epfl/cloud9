/*
 * WorkerStats.cpp
 *
 *  Created on: Jun 25, 2012
 *      Author: bucur
 */


#include "cloud9/worker/WorkerStats.h"

namespace cloud9 {
namespace worker {
namespace stats {

klee::Statistic total_replayed_instructions("TotalReplayedInstructions", "TRInst");
klee::Statistic total_wasted_instructions("TotalWastedInstructions", "TWInst");
klee::Statistic total_dropped_jobs("TotalDroppedJobs", "TDJobs");
klee::Statistic total_imported_jobs("TotalImportedJobs", "TIJobs");
klee::Statistic total_exported_jobs("TotalExportedJobs", "TEJobs");
klee::Statistic total_replayed_jobs("TotalReplayedJobs", "TRJobs");
klee::Statistic total_processed_jobs("TotalProcessedJobs", "TPJobs");
klee::Statistic total_tree_paths("TotalTreePaths", "TTPaths");

klee::Statistic active_states("ActiveStates", "AStates");
klee::Statistic job_count("JobCount", "JCount");

}
}
}
