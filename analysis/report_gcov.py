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


"""Reads .gcov files and generates HTML report out of that."""

__author__ = "sbucur@google.com (Stefan Bucur)"

import argparse
import fnmatch
import logging
import os
import SimpleHTTPServer
import SocketServer

import cloud9.coverage as coverage

def ParseGCovFile(file_name, source_filter):
  logging.info("Processing '%s'" % file_name)
  file_source = file_name

  covered_lines = set()
  coverable_lines = set()
  with open(file_name, "r") as f:
    for line in f:
      line = line.strip()

      cov_info, line_no, line_data = line.split(":", 2)
      cov_info = cov_info.strip()
      line_no = int(line_no.strip())

      if line_no == 0:
        key, value = line_data.split(":", 1)
        if key == "Source":
          file_source = value
          if source_filter and not source_filter.IsValid(file_source):
            break
      else:
        if cov_info == "-":  # Uncoverable line
          pass
        elif cov_info == "#####":  # Coverable, but uncovered line
          coverable_lines.add(line_no)
        else:  # Covered line
          coverable_lines.add(line_no)
          covered_lines.add(line_no)

  return file_source, coverable_lines, covered_lines


def main():
  logging.basicConfig(format="[%(levelname)s] %(message)s", level=logging.INFO)

  arg_parser = argparse.ArgumentParser(description="Compute GCOV code coverage.")
  arg_parser.add_argument("code_root", default=".",
                      help="Where to look for .gcov files.")
  arg_parser.add_argument("-c", "--coverable", help="A .coverable file")
  arg_parser.add_argument("-o", "--output", default="report",
                          help="The output directory of the report.")
  arg_parser.add_argument("--run-server", action="store_true", default=False,
                          help="Serve the produced content.")

  args = arg_parser.parse_args()

  if args.coverable:
    source_filter = coverage.LoadSourceFilter(args.coverable)
  else:
    source_filter = None
    
  coverable_lines = {}
  covered_lines = {}

  for dir_path, _, file_names in os.walk(args.code_root):
    for file_name in fnmatch.filter(file_names, '*.gcov'):
      file_source, source_coverable, source_covered = ParseGCovFile(
          os.path.join(dir_path, file_name), source_filter)
      coverable_lines[file_source] = source_coverable
      covered_lines[file_source] = source_covered
      
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
