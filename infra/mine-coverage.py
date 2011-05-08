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

from argparse import ArgumentParser
from coverageminer import CoverageMiner

def main():
    parser = ArgumentParser(description="Mine Cloud9 experiments.",
                            fromfile_prefix_chars="@")
    parser.add_argument("hosts", help="Available cluster machines")
    parser.add_argument("tests", nargs="*", help="Test names")
    parser.add_argument("-f", action="append", help="File containing test names")
    parser.add_argument("-t", action="store_true", default=False, help="Display in a tabular, human-readable format")
    parser.add_argument("-m", action="append", help="Mine only the specified machines")
    parser.add_argument("-c", nargs="+", help="Measure the time it takes to get a coverage level")
    parser.add_argument("-x", action="append", help="Mine coverage only for functions listed in the specified file")

    args = parser.parse_args()
    tests = args.tests[:]

    if args.f:
        for fname in args.f:
            f = open(fname, "r")
            tests.extend(f.read().split())
            f.close()
    
    ffilter = None
    if args.x:
        for fname in args.x:
            f = open(fname, "r")
            if ffilter is None:
                ffilter = []
            ffilter.extend(f.read().split())
            f.close()

    covminer = CoverageMiner(args.hosts, 
                             hfilter=args.m, 
                             ffilter=ffilter,
                             targetcov=map(float, args.c[1:]) if args.c else None)
    covminer.analyzeExperiments(tests)

    if args.c:
        covminer.printMinTimes(args.c[0])
    else:
        covminer.printCoverageStats("human" if args.t else "internal")

if __name__ == "__main__":
    main()
