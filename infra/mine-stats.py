/usr/bin/env python
#
# Cloud9 Parallel Symbolic Execution Engine
# 
# Copyright (c) 2011, Dependable Systems Laboratory, EPFL
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# All contributors are listed in CLOUD9-AUTHORS file.
#

import sys
import subprocess
import re
import math
import itertools

from common import readHosts, runBashScript, isExperimentRejected
from common import AverageEntry
from subprocess import PIPE
from argparse import ArgumentParser

class Stats:
    TOTAL_PROC_INSTRUCTIONS = 0
    TOTAL_PROC_JOBS = 1
    TOTAL_REPLAYED_JOBS = 14
    TOTAL_EXPORTED_JOBS = 8
    TOTAL_IMPORTED_JOBS = 9
    TOTAL_DROPPED_JOBS = 10
    TOTAL_FORKED_STATES = 15
    TOTAL_FINISHED_STATES = 16
    TOTAL_TREE_PATHS = 17
    TOTAL_REPLAY_INSTRUCTIONS = 20
    CURRENT_JOB_COUNT = 11
    CURRENT_ACTIVE_STATE_COUNT = 18
    CURRENT_STATE_COUNT = 19

class StatsEntry:
    def __init__(self, timestamp=None, stats=None):
        self.timestamp = timestamp
        self.stats = stats

class StatsMiner:
    def __init__(self, hostsName, hfilter=None):
        self.hosts, self.localhost = readHosts(hostsName)
        self.hfilter = set(hfilter) if hfilter else None

        self.pathRe = re.compile(r"^./([^/]+)/([^/-]+)-(\d+)(-(\d+))?/worker-(\d+)/c9-stats.txt$")

    def _logMsg(self, msg):
        print >>sys.stderr, "-- %s" % msg

    def analyzeExperiments(self, explist, wcfilter=None, samplerate=None):
        statsdb = { }

        for host in self.hosts:
            if self.hfilter and host not in self.hfilter:
                continue
            self._pollStats(host, explist, statsdb, wcfilter=wcfilter)

        aggdb = self._aggregateStats(statsdb, samplerate=samplerate)
        avgdb = self._averageUsefulWork(aggdb)
        
        return avgdb

    def _pollStats(self, host, testdirs, statsdb, skip=5, wcfilter=None):
        self._logMsg("Polling stats for host %s..." % host)
        proc = runBashScript("""
           ssh %(user)s@%(host)s 'bash -s' <<EOF
           cd %(expdir)s
           # The code below is run remotely
           for TESTDIR in %(testdirs)s; do            
               find ./\\$TESTDIR -name 'c9-stats.txt' | while read LINE; do
                   echo \\$LINE
                   sed -n '1~%(skip)d p; $ p;' \\$LINE
                   echo
               done
           done
           \nEOF""" % {
                "user": self.hosts[host]["user"],
                "host": host,
                "testdirs": " ".join(testdirs),
                "expdir": self.hosts[host]["expdir"],
                "skip": skip
                }, stdout=PIPE)

        data,_ = proc.communicate()
        entries = None

        for line in data.splitlines():
            line = line.strip()
            if not len(line):
                continue

            match = self.pathRe.match(line)
            if match:
                testdir, target, workercount, _, tgcount, workerID = match.groups()
                workercount = int(workercount)
                tgcount = int(tgcount) if tgcount else 1
                workerID = int(workerID)

                if wcfilter and workercount != wcfilter:
                    entries = None
                    continue
                if isExperimentRejected(testdir, target, workercount, tgcount):
                    entries = None
                    continue

                tgdata = statsdb.setdefault(target, {})
                expdata = tgdata.setdefault((testdir, tgcount), {})
                wrkdata = expdata.setdefault(workercount, {})
                entries = wrkdata.setdefault(workerID, [])
                continue

            if entries is None:
                continue

            tokpair = line.split(" ", 1)
            timestamp = float(tokpair[0])
            if len(tokpair) > 1:
                stats = dict([(int(x[0]),int(x[1])) 
                              for x in map(lambda token: token.split("="), tokpair[1].split())])
            else:
                stats = {}

            entry = StatsEntry(timestamp, stats)
            entries.append(entry)

        return

    def _aggregateStats(self, statsdb, samplerate=None):
        def aggEntries(entries):
            result = { }
            for entry in entries:
                for k,v in entry.stats.iteritems():
                    result[k] = result.get(k, 0) + v
            return result

        if not samplerate:
            samplerate = 60

        aggdb = { }
        for target, exps in statsdb.iteritems():
            tgdata = aggdb.setdefault(target, {})
            for tgtrial, exp in exps.iteritems():
                expdata = tgdata.setdefault(tgtrial, {})
                for workercount, entries in exp.iteritems():
                    wrkdata = expdata.setdefault(workercount, [])
                    iterators = dict([(k, iter(v)) for k,v in entries.iteritems()])
                    curvalues = dict([(k, iterators[k].next()) for k in iterators.iterkeys()])
                    aggvalues = dict(curvalues)

                    pointiter = (samplerate*i for i in itertools.count())
                    targetpoint = pointiter.next()
                    entry = None
                    while len(iterators):
                        k, entry = min(curvalues.iteritems(), key=lambda pair: pair[1].timestamp)

                        if targetpoint is not None and entry.timestamp > targetpoint:
                            aggregation = aggEntries(aggvalues.itervalues())
                            while targetpoint is not None and entry.timestamp > targetpoint:
                                wrkdata.append(StatsEntry(targetpoint, aggregation))
                                try:
                                    targetpoint = pointiter.next()
                                except StopIteration:
                                    targetpoint = None
                        try:
                            curvalues[k] = iterators[k].next()
                            aggvalues[k] = curvalues[k]
                        except StopIteration:
                            del iterators[k]
                            del curvalues[k]
                    if entry and targetpoint is not None:
                        wrkdata.append(StatsEntry(targetpoint, aggEntries(aggvalues.itervalues())))

        return aggdb

    def _averageUsefulWork(self, aggdb):
        avgdb = { }
        for target, exps in aggdb.iteritems():
            tgdata = avgdb.setdefault(target, {})
            
            for tgcount, exp in exps.iteritems():
                for workercount, entries in exp.iteritems():
                    wrkdata = tgdata.setdefault(workercount, {})
                    for entry in entries:
                        avg = wrkdata.setdefault(entry.timestamp, AverageEntry())
                        avg.entries.append(entry.stats.get(Stats.TOTAL_PROC_INSTRUCTIONS, 0) - 
                                           entry.stats.get(Stats.TOTAL_REPLAY_INSTRUCTIONS, 0))

            for workercount, entries in tgdata.iteritems():
                for timestamp, avg in entries.iteritems():
                    avg.computeAverage()

        return avgdb

    def printUsefulWork(self, aggdb, target="memcached"):
        tgdata = aggdb[target]
        for workercount in sorted(tgdata.keys()):
            entries = tgdata[workercount]
            print "%d:" % workercount,
            for timestamp in sorted(entries.keys()):
                entry = entries[timestamp]
                print "%d=%d,%.4f%%,%d" % (
                    timestamp, int(entry.average), 
                    100.0*int(entry.stdev)/int(entry.average) if int(entry.average) else 0.0, 
                    len(entry.entries)),
            print

def main():
    parser = ArgumentParser(description="Mine Cloud9 experiments.",
                            fromfile_prefix_chars="@")
    parser.add_argument("hosts", help="Available cluster machines")
    parser.add_argument("target", help="Name of the testing target")
    parser.add_argument("tests", nargs="*", help="Test names")
    parser.add_argument("-f", action="append", help="File containing test names")
    parser.add_argument("-m", action="append", help="Mine only the specified machines")
    parser.add_argument("-w", type=int, help="Mine only data specific to a number of workers")
    parser.add_argument("-s", type=int, help="Sample every number of specified seconds")

    args = parser.parse_args()
    tests = args.tests[:]

    if args.f:
        for fname in args.f:
            f = open(fname, "r")
            tests.extend(f.read().split())
            f.close()

    statminer = StatsMiner(args.hosts, hfilter=args.m)
    results = statminer.analyzeExperiments(tests, wcfilter=args.w, samplerate=args.s)
    statminer.printUsefulWork(results, args.target)

if __name__ == "__main__":
    main()
