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

"""
Common functionality across all the Python scripts.
"""

import subprocess
import math
import re
import glob

_HOSTS_DIR = "./hosts"
_CMDLINES_DIR = "./cmdlines"
_EXP_DIR = "./exp"
_KLEECMD_DIR = "./kleecmd"
_COVERABLE_DIR = "./coverable"
_REJECTED_DIR = "./rejects"

_REJECTION_RE = re.compile(r"([^/]+)/([^/-]+)-(\d+)(-(\d+))?")


def _getRejections():
    rejections = set()

    files = glob.glob("%s/*.txt" % _REJECTED_DIR)
    for fileName in files:
        f = open(fileName, "r")
        rejSet = f.read().split()
        f.close()

        for rej in rejSet:
            match = _REJECTION_RE.match(rej)
            if not match:
                continue

            testdir, target, workercount, _, tgcount = match.groups()
            workercount = int(workercount)
            tgcount = int(tgcount) if tgcount else 1
            
            rejections.add((testdir, target, workercount, tgcount))

    return rejections
            
_REJECTIONS = _getRejections()

class AverageEntry:
    def __init__(self):
        self.entries = []
        self.average = None
        self.stdev = None

    def _computeAverage(self, entries):
        average = sum(entries)/len(entries) if len(entries) else 0.0
        stdev = math.sqrt(sum([(x-average)*(x-average) for x in entries])/
                               (len(entries)-1)) if len(entries) > 1 else 0.0

        return average,stdev

    def computeAverage(self, fixoutliers=False):
        average, stdev = self._computeAverage(self.entries)
        if not fixoutliers or len(self.entries) < 4:
            self.average, self.stdev = average, stdev
            return
        
        newentries = filter(lambda x: abs(x-average) < stdev, self.entries)
        self.average, self.stdev = self._computeAverage(newentries)

    def __str__(self):
        return "(%s,%s)(%d)" % (
            "%.2f" % self.average if self.average else "-",
            "%.2f" % self.stdev if self.stdev else "-",
            len(self.entries)
            )
    def __repr__(self):
        return self.__str__()


def _getHostsPath(hosts):
    return "%s/%s.hosts" % (_HOSTS_DIR, hosts)

def _getCmdlinesPath(cmdlines):
    return "%s/%s.cmdlines" % (_CMDLINES_DIR, cmdlines)

def _getExpPath(exp):
    return "%s/%s.exp" % (_EXP_DIR, exp)

def _getKleeCmdPath(kleeCmd):
    return "%s/%s.kcmd" % (_KLEECMD_DIR, kleeCmd)

def runBashScript(script, **extra):
    return subprocess.Popen(["/bin/bash", "-c", script], **extra)

def getCoverablePath(coverable):
    return "%s/%s.coverable" % (_COVERABLE_DIR, coverable)

def readHosts(hostsFile):
    hosts = { }
    localhost = None
    f = open(_getHostsPath(hostsFile), "r")
    for line in f:
        if line.startswith("#"):
            continue
        tokens = line.split()

        host = tokens[0]
        entry = dict(zip(["host", "cores", "root", "user", "expdir", "targetdir"], tokens))
        entry["cores"] = int(entry["cores"])

        if entry["cores"] == 0:
            localhost = entry
        else:
            hosts[host] = entry
    f.close()

    return (hosts, localhost)

def readCmdlines(cmdlinesFile):
    cmdlines = { }
    f = open(_getCmdlinesPath(cmdlinesFile), "r")
    for line in f:
        if line.startswith("#"):
            continue
        name, cmdline = line.split(":")
        cmdlines[name] = cmdline
    f.close()

    return cmdlines

def readExp(exp):
    schedule = []
    f = open(_getExpPath(exp), "r")
    for line in f:
        if line.startswith("#"):
            continue
        tokens = line.split()
        if not len(tokens):
            continue
        stage = []
        index = 0
        currentCmdline, targetCount, currentCount = None, None, None
        allocation = None
        while index < len(tokens):
            if currentCmdline is None or targetCount == currentCount:
                if currentCmdline is not None:
                    stage.append((currentCmdline, targetCount, allocation))
                currentCmdline = tokens[index]
                targetCount = int(tokens[index+1])
                currentCount = 0
                allocation = []
            else:
                host, count = tokens[index], int(tokens[index+1])
                allocation.append((host, count))
                currentCount += count
            index += 2

        assert targetCount and targetCount == currentCount
        stage.append((currentCmdline, targetCount, allocation))
        schedule.append(stage)
                
    f.close()

    return schedule

def readKleeCmd(kleeCmd):
    f = open(_getKleeCmdPath(kleeCmd), "r")
    cmds = f.read().split()
    f.close()

    return cmds

def isExperimentRejected(testdir, target, workercount, tgcount):
    return (testdir, target, workercount, tgcount) in _REJECTIONS

################################################################################
# Rich Terminal

def bold(text):
    return "\033[1m%s\033[0m" % text

def faint(text):
    return "\033[2m%s\033[0m" % text
