#!/usr/bin/python
# 
# Copyright (c) 2012 Google Inc. All Rights Reserved.
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

"""Generates HTML view of source code."""

__author__ = "sbucur@google.com (Stefan Bucur)"

import logging
import SimpleHTTPServer
import SocketServer

import argparse

import cloud9.coverage as coverage

def main():
  logging.basicConfig(format="[%(levelname)s] %(message)s", level=logging.INFO)

  arg_parser = argparse.ArgumentParser(
      description="Generate GCOV files for Cloud9 coverage.")

  arg_parser.add_argument("code_root",
                          help="Where to look for source files.")
  arg_parser.add_argument("coverage_data",
                          help="The coverage information file.")
  arg_parser.add_argument("-c", "--coverable", help="A .coverable file.")
  arg_parser.add_argument("-o", "--output", default="report",
                          help="The output directory of the report.")
  arg_parser.add_argument("--run-server", action="store_true", default=False,
                          help="Serve the produced content.")

  args = arg_parser.parse_args()

  if args.coverable:
    source_filter = coverage.LoadSourceFilter(args.coverable)
  else:
    source_filter = None

  # Parse the coverage information
  with open(args.coverage_data, "r") as data_file:
    coverable_lines, covered_lines = coverage.ReadLastCoverageInfo(data_file)
    
  coverage_annotator = coverage.CoverageAnnotator(
      coverage_data=covered_lines, ground_coverage_data=coverable_lines, 
      source_filter=source_filter)

  annotation_data = coverage_annotator.AnnotateDirectory(args.code_root)
  
  renderer = coverage.AnnotationRenderer()
  renderer.RenderAnnotation(annotation_data, args.output)

  if args.run_server:
    http_handler = SimpleHTTPServer.SimpleHTTPRequestHandler
    httpd = SocketServer.TCPServer(("", 8000), http_handler)
  
    logging.info("Serving report at port %d" % 8000)
    httpd.serve_forever()


if __name__ == "__main__":
  main()
