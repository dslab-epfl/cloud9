//===-- Statistics.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATISTICS_H
#define KLEE_STATISTICS_H

#include "Statistic.h"

#include <vector>
#include <string>
#include <cassert>
#include <string.h>

namespace klee {
  class Statistic;
  class StatisticRecord {
    friend class StatisticManager;

  private:
    uint64_t *data;

  public:    
    StatisticRecord();
    StatisticRecord(const StatisticRecord &s);
    ~StatisticRecord() { delete[] data; }
    
    void zero();

    uint64_t getValue(const Statistic &s) const; 
    void incrementValue(const Statistic &s, uint64_t addend) const;
    StatisticRecord &operator =(const StatisticRecord &s);
    StatisticRecord &operator +=(const StatisticRecord &sr);
  };

  class StatisticManager {
  private:
    bool enabled;
    std::vector<Statistic*> stats;
    uint64_t *globalStats;

    unsigned totalIndices;
    uint64_t *indexedStats;
    std::vector<std::pair<bool, std::vector<char> > > changedIdxStats;

    StatisticRecord *contextStats;
    unsigned index;

    inline void recordChange(unsigned id, unsigned index) {
      if (changedIdxStats[id].first) {
        changedIdxStats[id].second[index] = 1;
      }
    }

  public:
    StatisticManager();
    ~StatisticManager();

    void useIndexedStats(unsigned totalIndices);

    StatisticRecord *getContext();
    void setContext(StatisticRecord *sr); /* null to reset */

    void setIndex(unsigned i) { index = i; }
    unsigned getIndex() { return index; }
    unsigned getNumStatistics() { return stats.size(); }
    Statistic &getStatistic(unsigned i) { return *stats[i]; }
    
    void registerStatistic(Statistic &s);
    void incrementStatistic(Statistic &s, uint64_t addend);
    uint64_t getValue(const Statistic &s) const;
    void incrementIndexedValue(const Statistic &s, unsigned index, 
                               uint64_t addend);
    uint64_t getIndexedValue(const Statistic &s, unsigned index) const;
    void setIndexedValue(const Statistic &s, unsigned index, uint64_t value);
    int getStatisticID(const std::string &name) const;
    Statistic *getStatisticByName(const std::string &name) const;

    void trackChanges(const Statistic &s);
    void resetChanges(const Statistic &s);
    void collectChanges(const Statistic &s, std::vector<std::pair<uint32_t, uint64_t> > &changes);
  };

  extern StatisticManager *theStatisticManager;

  inline void StatisticManager::incrementStatistic(Statistic &s, 
                                                   uint64_t addend) {
    if (enabled) {
      globalStats[s.id] += addend;
      if (indexedStats) {
        indexedStats[index*stats.size() + s.id] += addend;
        recordChange(s.id, index);

        if (contextStats) {
          contextStats->data[s.id] += addend;
        }
      }
    }
  }

  inline StatisticRecord *StatisticManager::getContext() {
    return contextStats;
  }
  inline void StatisticManager::setContext(StatisticRecord *sr) {
    contextStats = sr;
  }

  inline void StatisticRecord::zero() {
    ::memset(data, 0, sizeof(*data)*theStatisticManager->getNumStatistics());
  }

  inline StatisticRecord::StatisticRecord() 
    : data(new uint64_t[theStatisticManager->getNumStatistics()]) {
    zero();
  }

  inline StatisticRecord::StatisticRecord(const StatisticRecord &s) 
    : data(new uint64_t[theStatisticManager->getNumStatistics()]) {
    ::memcpy(data, s.data, 
             sizeof(*data)*theStatisticManager->getNumStatistics());
  }

  inline StatisticRecord &StatisticRecord::operator=(const StatisticRecord &s) {
    ::memcpy(data, s.data, 
             sizeof(*data)*theStatisticManager->getNumStatistics());
    return *this;
  }

  inline void StatisticRecord::incrementValue(const Statistic &s, 
                                              uint64_t addend) const {
    data[s.id] += addend;
  }
  inline uint64_t StatisticRecord::getValue(const Statistic &s) const { 
    return data[s.id]; 
  }

  inline StatisticRecord &
  StatisticRecord::operator +=(const StatisticRecord &sr) {
    unsigned nStats = theStatisticManager->getNumStatistics();
    for (unsigned i=0; i<nStats; i++)
      data[i] += sr.data[i];
    return *this;
  }

  inline uint64_t StatisticManager::getValue(const Statistic &s) const {
    return globalStats[s.id];
  }

  inline void StatisticManager::incrementIndexedValue(const Statistic &s, 
                                                      unsigned index,
                                                      uint64_t addend) {
    indexedStats[index*stats.size() + s.id] += addend;
    recordChange(s.id, index);
  }

  inline uint64_t StatisticManager::getIndexedValue(const Statistic &s, 
                                                    unsigned index) const {
    return indexedStats[index*stats.size() + s.id];
  }

  inline void StatisticManager::setIndexedValue(const Statistic &s, 
                                                unsigned index,
                                                uint64_t value) {
    indexedStats[index*stats.size() + s.id] = value;
    recordChange(s.id, index);
  }

  inline void StatisticManager::trackChanges(const Statistic &s) {
    changedIdxStats[s.id].first = true;

    resetChanges(s);
  }

  inline void StatisticManager::resetChanges(const Statistic &s) {
    assert(changedIdxStats[s.id].first);

    changedIdxStats[s.id].second.clear();
    changedIdxStats[s.id].second.resize(totalIndices, 0);
  }

  inline void StatisticManager::collectChanges(const Statistic &s, std::vector<std::pair<uint32_t, uint64_t> > &changes) {
    assert(changedIdxStats[s.id].first);

    for (unsigned int i = 0; i < totalIndices; i++) {
      if (changedIdxStats[s.id].second[i]) {
        changes.push_back(std::make_pair(i, indexedStats[i*stats.size() + s.id]));
      }
    }
  }
}

#endif
