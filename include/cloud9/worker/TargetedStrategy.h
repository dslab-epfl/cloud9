/*
 * TargetedStrategy.h
 *
 *  Created on: Sep 17, 2010
 *      Author: stefan
 */

#ifndef TARGETEDSTRATEGY_H_
#define TARGETEDSTRATEGY_H_

#include "cloud9/worker/CoreStrategies.h"
#include "klee/ForkTag.h"

#include <set>
#include <vector>

namespace cloud9 {

namespace worker {

class TargetedStrategy: public BasicStrategy {
public:
  typedef std::set<std::string> interests_t;
private:
  typedef std::map<ExecutionJob*, unsigned> job_set_t;
  typedef std::vector<ExecutionJob*> job_vector_t;

  typedef std::pair<job_set_t, job_vector_t> job_container_t;

  WorkerTree *workerTree;

  job_container_t workingSet;
  job_container_t interestingJobs;
  job_container_t uninterestingJobs;

  interests_t localInterests;

  char adoptionRate;

  unsigned int explosionLimitSize;
  unsigned int workingSetSize;

  ExecutionJob *selectRandom(job_container_t &container);

  void insertInterestingJob(ExecutionJob *job, job_container_t &wset, job_container_t &others);
  void removeInterestingJob(ExecutionJob *job, job_container_t &wset, job_container_t &others);

  void insertJob(ExecutionJob *job, job_container_t &container);
  void removeJob(ExecutionJob *job, job_container_t &container);

  bool isInteresting(klee::ForkTag forkTag, interests_t &interests);
  bool isInteresting(ExecutionJob *job, interests_t &interests);
  void adoptJobs();

  unsigned int selectForExport(job_container_t &container,
      interests_t &interests, std::vector<ExecutionJob*> &jobs,
      unsigned int maxCount);
public:
  TargetedStrategy(WorkerTree *_workerTree, JobManager *_jobManager);
  virtual ~TargetedStrategy() { }

  virtual ExecutionJob* onNextJobSelection();

  virtual void onJobAdded(ExecutionJob *job);
  virtual void onRemovingJob(ExecutionJob *job);

  unsigned getInterestingCount() const { return workingSet.first.size() + interestingJobs.first.size(); }
  unsigned getUninterestingCount() const { return uninterestingJobs.first.size(); }

  void updateInterests(interests_t &interests);
  unsigned int selectForExport(interests_t &interests,
      std::vector<ExecutionJob*> &jobs, unsigned int maxCount);

  static interests_t anything;
};

}

}

#endif /* TARGETEDSTRATEGY_H_ */
