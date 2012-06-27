/*
 * WorkerStats.h
 *
 *  Created on: Jun 25, 2012
 *      Author: bucur
 */

#ifndef WORKERSTATS_H_
#define WORKERSTATS_H_

#include "klee/Statistic.h"

namespace cloud9 {
namespace worker {
namespace stats {

extern klee::Statistic total_replayed_instructions;
extern klee::Statistic total_wasted_instructions;
extern klee::Statistic total_dropped_jobs;
extern klee::Statistic total_imported_jobs;
extern klee::Statistic total_exported_jobs;
extern klee::Statistic total_replayed_jobs;
extern klee::Statistic total_processed_jobs;
extern klee::Statistic total_tree_paths;

extern klee::Statistic active_states;
extern klee::Statistic job_count;

}
}
}


#endif /* WORKERSTATS_H_ */
