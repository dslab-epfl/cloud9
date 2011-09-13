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
Implements the ExperimentManager class.
"""

import subprocess
import os.path
import time
import re
import signal

from common import readHosts, readCmdlines, readExp, readKleeCmd, getCoverablePath, runBashScript
from common import bold, faint
from datetime import datetime, timedelta

from subprocess import PIPE

DEFAULT_BASE_PORT = 10337
DEFAULT_EXP_DURATION = 3600
DEFAULT_INTER_SLEEP = 5
MONITOR_INCREMENT = 3

WORKER_PATH = "Release+Asserts/bin/c9-worker"
LB_PATH = "Release+Asserts/bin/c9-lb"
KLEE_PATH = "Release+Asserts/bin/klee"

class ExperimentManager:
    def __init__(self, hostsName, cmdlinesName, expName, kleeCmdName, coverableName,
                 uid=None, uidprefix="test", debugcomm=False, duration=DEFAULT_EXP_DURATION,
                 balancetout=None, strategy=None, subprocKill=True, basePort=DEFAULT_BASE_PORT):
        self.hosts, self.localhost = readHosts(hostsName)
        self.cmdlines = readCmdlines(cmdlinesName)
        self.exp = readExp(expName)
        self.kleeCmd = readKleeCmd(kleeCmdName)
        self.coverable = getCoverablePath(coverableName)
        self.uid = uid if uid else self._generateUID(uidprefix)
        self.debugcomm = debugcomm
        self.starttime = None
        self.duration = duration
        self.balancetout = balancetout
        self.strategy = strategy
        self.subprocKill = subprocKill
        self.basePort = basePort

        self._logMsg("Using experiment name: %s" % bold(self.uid))
        self._logMsg("Using as localhost: %s" % self.localhost["host"])

    def initHosts(self):
        self._prepareLocalHost()

        for host in self.hosts.iterkeys():
            self._logMsg("Initializing host '%s'..." % host)
            if self.hosts[host]["cores"] == 0: continue
            self._prepareRemoteHost(host)

    def runExperiment(self):
        # First, make sure all the hosts are configured
        self._checkSchedule()

        self.starttime = datetime.now()

        tgcounters = { }

        # Initializing port mappings
        ports = dict((host, self.basePort) for host in self.hosts)
        ports[self.localhost["host"]] = self.basePort
        
        for stageIndex, stage in enumerate(self.exp):
            self._logMsg("Running stage %d of the experiment." % (stageIndex + 1))
            time.sleep(DEFAULT_INTER_SLEEP)

            processes = {}

            for item in stage:
                target, workercount, allocs = item[0], item[1], item[2]
                tgcounters[(target, workercount)] = tgcounters.get((target, workercount), 0) + 1
                tgcounter = tgcounters[(target, workercount)]

                # Allocate the load balancer
                lbPort = ports[self.localhost["host"]]; ports[self.localhost["host"]] += 1
                lbProc = self._runLB(port=lbPort, 
                                     target=target, 
                                     workerCount=workercount,
                                     targetcounter=tgcounter)
                
                processes[(target, workercount, -1, tgcounter)] = lbProc

                workerID = 1

                for alloc in allocs:
                    host, alloccount = alloc[0], alloc[1]
                    for i in range(alloccount):
                        workerProc = self._runWorker(host=host, port=ports[host], 
                                                     lbHost=self.localhost["host"], lbPort=lbPort, 
                                                     target=target, workerID=workerID,
                                                     workerCount=workercount,
                                                     targetcounter=tgcounter)
                        processes[(target, workercount, workerID, tgcounter)] = workerProc

                        workerID += 1
                        ports[host] += 1

            # Waiting for everything to finish...
            self._monitorProcs(processes, self.duration, showID=True)
            if not len(processes):
                continue
            
            if self.subprocKill:
                self._killAllProcesses(processes, "SIGINT")
            else:
                self._killAll("SIGINT")

            self._monitorProcs(processes, 200)
            if len(processes) == 0:
                continue

            while True:
                # Here we risk going into an infinite loop, but it's better than aborting
                if self.subprocKill:
                    self._killAllProcesses(processes, "SIGINT")
                else:
                    self._killAll("SIGKILL", aggressive=True)
                self._monitorProcs(processes, 40)
                if len(processes) == 0:
                    break
                
    def _killAll(self, signal, aggressive=False):
        self._logMsg("Sending the %s signal..." % signal)
        for host in self.hosts:
            if host == self.localhost["host"]:
                continue
            self._killAllRemote(host, signal=signal, aggressive=aggressive)

        self._killAllLocal(signal=signal)

    def _killAllProcesses(self, processes, sig):
        self._logMsg("Sending the %s signal to the active processes..." % sig)
        for proc in processes.itervalues():
            proc.send_signal(getattr(signal, sig))

    def _monitorProcs(self, processes, duration, sleeptime=1, showID=False):
        targetTime = datetime.now()
        delta = timedelta(seconds=MONITOR_INCREMENT)
        totalPassed = 0

        targetTime = targetTime + delta
        while totalPassed < duration:
            time.sleep(sleeptime)
            if targetTime > datetime.now(): continue

            cleanups = []
            for (target, workercount, workerID, tgcounter), proc in processes.iteritems():
                if proc.poll() is not None:
                    cleanups.append((target, workercount, workerID, tgcounter))
                    if workerID < 0:
                        self._logMsg("The load balancer for target '%s'(%d) terminated.%s" % (
                                target, workercount,
                                (" ID: %s" % self._getExperimentID(target, workercount, tgcounter)) if showID else ""))
                    else:
                        self._logMsg("Worker %d for target '%s'(%d) terminated.%s" % (
                                workerID, target, workercount,
                                (" ID: %s" % self._getExperimentID(target, workercount, tgcounter) if showID else "")))

            for item in cleanups: del processes[item]

            if not len(processes):
                break
            
            targetTime = targetTime + delta
            totalPassed += MONITOR_INCREMENT

        return totalPassed

    def _generateUID(self, uidprefix):
        today = datetime.now()
        return uidprefix + "-" + "-".join(map(lambda x: "%02d" % x, today.timetuple()[0:6]))

    def _getExperimentID(self, target, workerCount, targetcounter):
        return  "%s/%s-%d%s" % (self.uid, target, workerCount,
                                ("-%d" % targetcounter) if targetcounter > 1 else "")

    def _checkSchedule(self):
        """Make sure the hosts involved in the schedule are configured."""
        for stage in self.exp:
            for item in stage:
                target, workercount, allocs = item[0], item[1], item[2]
                if target not in self.cmdlines:
                    self._logMsg("Target '%s' not configured. Aborting..." % target)
                    exit(1)
                totalcount = 0
                for alloc in allocs:
                    if alloc[0] not in self.hosts:
                        self._logMsg("Host '%s' not configured. Aborting..." % alloc[0])
                        exit(1)
                    totalcount += alloc[1]
                if totalcount != workercount and not (totalcount == 0 and workercount == 1):
                    self._logMsg("Invalid host allocation for target '%s'. Aborting..." % target)
                    exit(1)
                
        self._logMsg("Schedule verification complete.")

    def _printUsage(self):
        print >> sys.stderr, """Usage:
  %s HOSTS CMDLINES EXP KLEECMD COVERABLE
""" % sys.argv[0]

    def _killAllRemote(self, host, signal="SIGINT", aggressive=False, freq=5):
        proc = runBashScript("""
            ssh -o StrictHostKeyChecking=no %(user)s@%(host)s 'bash -s' <<EOF
            # The code below is run remotely
            while true; do
              DONE="true"
              killall -%(signal)s %(worker)s %(klee)s %(lb)s && DONE="false"
              %(mild)s && break
              \\$DONE && break
              echo "Tasks still running. Waiting more time..."
              sleep %(freq)d
            done
            \nEOF""" % {
                "user": self.hosts[host]["user"],
                "host": host,
                "signal": signal,
                "worker": os.path.basename(WORKER_PATH),
                "lb": os.path.basename(LB_PATH),
                "klee": os.path.basename(KLEE_PATH),
                "mild": "false" if aggressive else "true",
                "freq": freq
                })

        proc.wait()

    def _killAllLocal(self, signal="SIGINT"):
        proc = runBashScript("""
            killall -%(signal)s %(worker)s
            killall -%(signal)s %(klee)s
            killall -%(signal)s %(lb)s""" % {
                "signal": signal,
                "worker": os.path.basename(WORKER_PATH),
                "lb": os.path.basename(LB_PATH),
                "klee": os.path.basename(KLEE_PATH)
                })

        proc.wait()

    def _prepareRemoteHost(self, host, cleanCores=True):
        proc = runBashScript("""
            ssh -o StrictHostKeyChecking=no %(user)s@%(host)s 'bash -s' <<EOF && \
            scp %(coverage)s %(user)s@%(host)s:%(expdir)s/%(newdir)s/$(basename %(coverage)s)
            # The code below is run remotely
            if [ ! -f %(root)s/%(worker)s ]; then echo "Cannot find the Cloud9 worker executable: %(root)s/%(worker)s";  exit 1; fi
            if [ ! -f %(root)s/%(klee)s ]; then echo "Cannot find the Klee executable: %(root)s/%(klee)s"; exit 1; fi
            mkdir -p %(expdir)s/%(newdir)s
            %(cleancores)s && find %(expdir)s -name 'core' | xargs rm -f
            if [ -h %(expdir)s/last ]; then rm -f %(expdir)s/last; fi
            [ ! -a %(expdir)s/last ] && ln -s %(expdir)s/%(newdir)s %(expdir)s/last
            \nEOF""" % {
                "user": self.hosts[host]["user"],
                "host": host,
                "coverage": self.coverable,
                "root": self.hosts[host]["root"],
                "worker": WORKER_PATH,
                "klee": KLEE_PATH,
                "expdir": self.hosts[host]["expdir"], 
                "newdir": self.uid,
                "cleancores": "true" if cleanCores else "false"
                })

        if proc.wait() != 0:
            self._logMsg("Unable to initialize host '%s'. Aborting..." % host)
            exit(1)
        else:
            self._logMsg("Initialization complete for host '%s'." % host)

    def _prepareLocalHost(self):
        proc = runBashScript("""
            if [ ! -f %(root)s/%(lb)s ]; then echo "Cannot find the load balancer executable: %(root)s/%(lb)s"; exit 1; fi
            mkdir -p %(expdir)s/%(newdir)s
            if [ -h %(expdir)s/last ]; then rm -f %(expdir)s/last; fi
            [ ! -a %(expdir)s/last ] && ln -s %(expdir)s/%(newdir)s %(expdir)s/last""" % {
                "root": self.localhost["root"],
                "lb": LB_PATH,
                "expdir": self.localhost["expdir"],
                "newdir": self.uid
                })

        if proc.wait() != 0:
            self._logMsg("Unable to initialize the local host ('%s'). Aborting..." % self.localhost["host"])
            exit(1)
        else:
            self._logMsg("Local host initialized.")

    def _runLB(self, port, target, workerCount, targetcounter):
        logdir = "%s/%s" % (
            self.localhost["expdir"],
            self._getExperimentID(target, workerCount, targetcounter))
        proc = runBashScript("""
            mkdir -p %(logdir)s
            %(root)s/%(lb)s -address %(address)s -port %(port)d %(debugcomm)s %(btout)s &>%(logfile)s""" % {
                "logdir": logdir,
                "root": self.localhost["root"],
                "lb": LB_PATH,
                "address": self.localhost["host"],
                "port": port,
                "debugcomm": "-debug-worker-communication" if self.debugcomm else "",
                "btout": ("-balance-tout %d" % self.balancetout) if self.balancetout else "",
                "logfile": "%s/out-lb.txt" % logdir
                })

        self._logMsg("Load balancer created for target '%s'(%d) on port %d." % (target, workerCount, port))

        return proc

    def _runWorker(self, host, port, lbHost, lbPort, target, workerID, workerCount, targetcounter):
        logdir = "%s/%s" % (
            self.localhost["expdir"],
            self._getExperimentID(target, workerCount, targetcounter))
        if self.cmdlines[target].startswith("/"):
            cmdline = self.cmdlines[target]
        else:
            cmdline = "%s/%s" % (self.hosts[host]["targetdir"], self.cmdlines[target])

        proc = runBashScript("""
            mkdir -p %(logdir)s
            ssh -o StrictHostKeyChecking=no %(user)s@%(host)s 'bash -s' <<EOF &>%(logfile)s
            # The code below is run remotely
            mkdir -p %(expdir)s
            cd %(expdir)s
            ulimit -c unlimited
            setarch $(arch) -R %(root)s/%(worker)s -c9-lb-host %(lbhost)s -c9-lb-port %(lbport)d \
              -c9-local-host %(lhost)s -c9-local-port %(lport)d %(jobsel)s \
              -output-dir %(outdir)s \
              %(kcmd)s %(debugcomm)s %(debugcov)s \
              --max-time %(maxtime)d --coverable-modules %(coverable)s \
              %(cmdline)s
            \nEOF""" % {
                "user": self.hosts[host]["user"],
                "host": host,
                "expdir": "%s/%s" % (
                    self.hosts[host]["expdir"],
                    self._getExperimentID(target, workerCount, targetcounter)),
                "root": self.hosts[host]["root"],
                "worker": WORKER_PATH,
                "lbhost": lbHost,
                "lbport": lbPort,
                "lhost": host,
                "lport": port,
                "jobsel": " ".join(["-c9-job-%s" % x for x in 
                                    (self.strategy.split(",") 
                                     if self.strategy 
                                     else ["random-path", "cov-opt"])]),
                "outdir": "worker-%d" % workerID,
                "kcmd": " ".join(self.kleeCmd),
                "debugcomm": "--debug-lb-communication" if self.debugcomm else "",
                "debugcov": "--debug-coverable-instr" if self.debugcomm else "",
                "maxtime": self.duration,
                "coverable": "../%s" % os.path.basename(self.coverable),
                "cmdline": cmdline,
                "logdir": logdir,
                "logfile": "%s/out-worker-%d.txt" % (logdir, workerID) 
                })

        self._logMsg("Worker %d created on %s, port %d (lb. port %d) for target '%s'(%d)." % (workerID, host, port, lbPort, target, workerCount))

        return proc

    def _logMsg(self, msg):
        if self.starttime:
            duration = datetime.now() - self.starttime
            prefix = "%01dd %02d:%02d:%02d.%03d" % (duration.days, 
                                          duration.seconds // 3600,
                                          (duration.seconds % 3600) // 60,
                                          (duration.seconds % 3600) % 60,
                                          duration.microseconds // 1000)
        else:
            prefix = "-"*15;
        print "%s %s" % (faint("[%s]" % prefix), msg) 
