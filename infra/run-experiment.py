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

from expmanager import ExperimentManager
from argparse import ArgumentParser
from expmanager import DEFAULT_BASE_PORT

def main():
    parser = ArgumentParser(description="Run Cloud9 experiments.",
                            fromfile_prefix_chars="@")
    parser.add_argument("hosts", help="Available cluster machines")
    parser.add_argument("cmdlines", help="Command lines of the testing targets")
    parser.add_argument("exp", help="The experiment schedule file")
    parser.add_argument("kleecmd", help="The command line parameters to pass to Klee")
    parser.add_argument("coverable", help="The file containing the coverable files")
    parser.add_argument("--debugcomm", action="store_true", default=False, 
                        help="Enable worker communication debugging output (*very* verbose)")
    parser.add_argument("-p", "--prefix", default="test",
                        help="Prefix for the testing output directory")
    parser.add_argument("-t", "--duration", default=3600, type=int,
                        help="The duration of each experiment")
    parser.add_argument("--lb-stop", type=int,
                        help="The duraction of load balancing")
    parser.add_argument("--strategy", help="Worker search strategy.")
    parser.add_argument("--base-port", type=int, default=DEFAULT_BASE_PORT, 
                        help="Base port to use.")
    
    args = parser.parse_args()

    manager = ExperimentManager(args.hosts, args.cmdlines, args.exp,
                                args.kleecmd, args.coverable,
                                debugcomm=args.debugcomm, uidprefix=args.prefix,
                                duration=args.duration,
                                balancetout=args.lb_stop,
                                strategy=args.strategy,
                                basePort=args.base_port)
    manager.initHosts()
    manager.runExperiment()

if __name__ == "__main__":
    main()
