#!/usr/bin/env python
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

from common import readHosts, readCmdlines
from argparse import ArgumentParser

SPAN_MACHINES=False

def needNewIteration(current, needed):
    if SPAN_MACHINES:
        return needed > sum(current)

    for value in current:
        if needed <= value:
            return False
        
    return True

def printSchedule(hosts, cmdlines, workerCountList, 
                  hostsFilter=None, cmdFilter=None):
    hostsFilter = set(hostsFilter) if hostsFilter else None
    cmdFilter = set(cmdFilter) if cmdFilter else None

    # Put the finest grained jobs first
    workerCountList = sorted(workerCountList)
    
    # Sort the hosts
    hostNames = filter(lambda host: hosts[host]["cores"] > 0, hosts)
    if hostsFilter:
        hostNames = filter(lambda host: host in hostsFilter, hostNames)
    hostNames = sorted(hostNames, key=lambda host: hosts[host]["cores"], reverse=True)

    # Sort the command lines
    cmdNames = sorted(cmdlines)
    if cmdFilter:
        cmdNames = filter(lambda cmd: cmd in cmdFilter, cmdNames)
    
    currentCores = []
    totalCores = 0
    needNL = False

    for workerCount in workerCountList:
        effectiveWC = workerCount if workerCount > 0 else 1
        for cmdName in cmdNames:
            # If nothing can be scheduled at this point, reset the resource counters
            # and start a new iteration
            if needNewIteration(currentCores, effectiveWC):
                currentCores = [hosts[hostName]["cores"] for hostName in hostNames]
                totalCores = sum(currentCores)
                if needNL:
                    print
                    needNL = False
                if needNewIteration(currentCores, effectiveWC):
                    print >> sys.stderr, "Not enough cores"
                    exit(1)

            print cmdName, workerCount,

            neededCores = effectiveWC
            for i, host in enumerate(hostNames):
                if neededCores <= currentCores[i]:
                    print host, neededCores,
                    needNL = True
                    currentCores[i] -= neededCores
                    totalCores -= neededCores
                    neededCores = 0
                    break

                if SPAN_MACHINES and currentCores[i] > 0:
                    print host, currentCores[i],
                    needNL = True
                    totalCores -= currentCores[i]
                    neededCores -= currentCores[i]
                    currentCores[i] = 0

            assert neededCores == 0, "Not enough cores"

    print
                    
                
def main():
    parser = ArgumentParser(description="Schedule Cloud9 experiments.")
    parser.add_argument("hosts", help="Avaliable cluster machines")
    parser.add_argument("cmdlines", help="Command lines of the testing targets")
    parser.add_argument("workercount", type=int, nargs="+", help="Worker counts")
    parser.add_argument("-c", help="Filter command lines using the specified file")
    parser.add_argument("-m", action="append", help="Schedule only on the specificed machines")

    args = parser.parse_args()

    hosts, localhost = readHosts(args.hosts)
    cmdlines = readCmdlines(args.cmdlines)
    cmdFilter = None

    if args.c:
        f = open(args.c, "r")
        cmdFilter = f.read().split()
        f.close()

    printSchedule(hosts, cmdlines, args.workercount,
                  cmdFilter=cmdFilter,
                  hostsFilter=args.m)

if __name__ == "__main__":
    main()
