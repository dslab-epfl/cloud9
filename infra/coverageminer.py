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

import traceback
import sys
import subprocess
import re
import math

from common import readHosts, runBashScript, isExperimentRejected
from common import AverageEntry
from subprocess import PIPE

class ToolData:
    def __init__(self):
        self.coverage = { }
        self.maxcoverage = { }
        self.mintime = { }


class CoverageMiner:
    def __init__(self, hostsName, hfilter=None, ffilter=None, targetcov=None):
        self.hosts, self.localhost = readHosts(hostsName)
        self.hfilter = set(hfilter) if hfilter else None
        self.ffilter = set(ffilter) if ffilter else None
        self.targetcov = targetcov

        self.pathRe = re.compile(r"^./([^/]+)/([^/-]+)-(\d+)(-(\d+))?/worker-(\d+)/c9-coverage.txt$")
        self.covDataRe = re.compile(r"^\d+/\d+\(([0-9.]+)\)$")

    def _logMsg(self, msg):
        print >>sys.stderr, "-- %s" % msg

    def analyzeExperiments(self, explist):
        self.coveragedb = { }

        for host in self.hosts:
            if self.hfilter and host not in self.hfilter:
                continue
            self._pollCoverage(host, explist, self.coveragedb)

        self._computeExtremeValues(self.coveragedb)

    def _computeExtremeValues(self, coveragedb):
        for tool, tdata in coveragedb.iteritems():
            for workercount, datasets in tdata.coverage.iteritems():
                for tgid, dataset in datasets.iteritems():
                    maxcov = self._getMaxCoverage(dataset)
                    maxcovdict = coveragedb[tool].maxcoverage.setdefault(workercount, {})
                    maxcovdict[tgid] = maxcov

                    if self.targetcov:
                        for tcov in self.targetcov:
                            mintime = self._getCoverageTime(dataset, tcov)
                            if mintime:
                                mintimedict = coveragedb[tool].mintime.setdefault(workercount, {}).setdefault(tcov, {})
                                mintimedict[tgid] = mintime

    def _getMaxCoverage(self, dataset):
        maxentry = max(dataset, key=lambda entry: entry[1])
        return maxentry[1]

    def _getCoverageTime(self, dataset, cov):
        validset = filter(lambda entry: entry[1] >= cov, dataset)
        if not validset:
            return None
        minentry = min(validset, key=lambda entry: entry[0])
        return minentry[0]

    def _pollCoverage(self, host, testdirs, coveragedb, skip=5):
        self._logMsg("Polling coverage for host %s..." % host)
        proc = runBashScript("""
           ssh %(user)s@%(host)s 'bash -s' <<EOF
           cd %(expdir)s
           # The code below is run remotely
           for TESTDIR in %(testdirs)s; do            
               find ./\\$TESTDIR -name 'c9-coverage.txt' | while read LINE; do
                   echo \\$LINE
                   sed %(filter)s \\$LINE
               done
           done
           \nEOF""" % {
                "user": self.hosts[host]["user"],
                "host": host,
                "testdirs": " ".join(testdirs),
                "expdir": self.hosts[host]["expdir"],
                "filter": ("-n '1~%d p; $ p;'" % skip) if self.targetcov else "'$!N;$!D;'"
}, stdout=PIPE)

        data,_ = proc.communicate()

        targetData = None

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

                if isExperimentRejected(testdir, target, workercount, tgcount):
                    targetData = None
                    continue

                targetData = coveragedb.setdefault(target, ToolData())
                continue

            if not targetData:
                continue

            try:
                tokens = line.split()
                timestamp = float(tokens[0])
                globcov = None
                total = 0
                covered = 0
                for t in tokens[1:]:
                    (k, v) = t.split("=")
                    (newcovAdd, totalAdd) = (int(x) for x in v.split("(")[0].split("/")[:2]) # 26/30(86.67)
                    if self.ffilter:
                        if k in self.ffilter:
                            total += totalAdd
                            covered += newcovAdd
                    elif k == "<global>":
                        total = totalAdd
                        covered = newcovAdd
                        break

                globcov = 0 if total == 0 else 100. * covered / total
                newcov = globcov
                covdict = targetData.coverage.setdefault(workercount, {})
                dataset = covdict.setdefault((testdir,tgcount), [])
                dataset.append((timestamp, newcov))
            except:
                e_desc = traceback.format_exc()
                self._logMsg("NOTE: Cannot process covdata '%s' on host '%s', target '%s'(%d), id %d, error: %s" % \
                    (line, host, target, workercount, workerID, e_desc))

    def _extractKeys(self, coveragedb):
        workerSet = set()
        for name, data in coveragedb.iteritems():
            workerSet = workerSet.union(data.maxcoverage.keys())

        workerList = sorted(workerSet)
        toolList = sorted(coveragedb.keys())

        return workerList, toolList

    def _printCoverageStatsTable(self, coveragedb, outputcsv=False):        
        workerList, toolList = self._extractKeys(coveragedb)
        separator = "," if outputcsv else ""

        print "%15s  " % "Tool",
        for workerCount in workerList:
            print "%22s" % ("%d W" % workerCount if workerCount else "Klee"),
        print
        print "="*80

        for tool in toolList:
            print "%15s%s  " % (tool, separator),
            for workerCount in workerList:
                covValues = coveragedb[tool].maxcoverage.get(workerCount)
                if covValues is None:
                    print "%22s%s" % ("-", separator),
                    continue
                
                avg = sum(covValues.values())/len(covValues.values())
                stdev = math.sqrt(sum([(x-avg)*(x-avg) for x in covValues.values()])/(len(covValues)-1)) \
                    if covValues and len(covValues) > 1 else 0.0
                print "%22s%s" % (
                    "%7s +/- %6s %4s" % (
                        "%.2f%%" % avg,
                        "%.2f%%" % stdev,
                        "(%d)" % len(covValues)
                        ), separator
                    ),
            print

    def _printCoverageStatsInternal(self, coveragedb):
        workerList, toolList = self._extractKeys(coveragedb)

        for tool in toolList:
            print tool,
            for workerCount in workerList:
                covValues = coveragedb[tool].maxcoverage.get(workerCount)
                avg = sum(covValues.values())/len(covValues.values()) if covValues else 0.0
                stdev = math.sqrt(sum([(x-avg)*(x-avg) for x in covValues.values()])/(len(covValues)-1)) \
                    if covValues and len(covValues) > 1 else 0.0
                print "%s:%s" % (workerCount, 
                                 ",".join(["%.2f%%" % x for x in covValues.values() + [avg,stdev]]) if covValues is not None else "-"),
            print

    def printCoverageStats(self, format="human"):
        if not self.coveragedb:
            self._logMsg("No coverage information.")
            return

        if format == "human":
            self._printCoverageStatsTable(self.coveragedb, outputcsv=False)
        elif format == "csv":
            self._printCoverageStatsTable(self.coveragedb, outputcsv=True)
        elif format == "internal":
            self._printCoverageStatsInternal(self.coveragedb)

    def printMinTimes(self, target):
        if not self.coveragedb:
            self._logMsg("No coverage information.")

        for workercount in sorted(self.coveragedb[target].mintime.keys()):
            print "%d: " % workercount,
            for tcov in sorted(self.targetcov):
                values = self.coveragedb[target].mintime[workercount].get(tcov)
                average = AverageEntry()
                if values:
                    average.entries = list(values.values())
                    average.computeAverage(fixoutliers=True)
                print "%d=%s" % (
                    tcov,
                    "%d,%d,%d" % (
                            int(average.average),
                            int(average.stdev),
                            len(average.entries)) if average.average else "-"),
            print
