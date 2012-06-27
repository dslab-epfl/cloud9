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

"""GCov-style file generation from Cloud9 coverage logs."""

__author__ = "sbucur@google.com (Stefan Bucur)"


import argparse
import fnmatch
import logging
import os
import struct
import sys

import cloud9.coverage as coverage


def main():
  logging.basicConfig(format="[%(levelname)s] %(message)s", level=logging.INFO)

  parser = argparse.ArgumentParser(
      description="Generate GCOV files for Cloud9 coverage.")

  parser.add_argument("code_root",
                      help="Where to look for source files.")
  parser.add_argument("coverage_data",
                      help="The coverage information file.")
  parser.add_argument("-c", "--coverable", help="A .coverable file.")
  parser.add_argument("-e", "--extension", default="c9gcov")

  args = parser.parse_args()

  if args.coverable:
    with open(args.coverable, "r") as cov_file:
      source_filter = [line.strip() for line in cov_file.readlines()]
  else:
    source_filter = None

  with open(args.coverage_data, "r") as data_file:
    coverable_lines, covered_lines = coverage.ReadLastCoverageInfo(data_file)

  total_coverable_count = 0
  total_covered_count = 0

  for dirpath, _, filenames in os.walk(args.code_root):
    for filename in filenames:
      if not (fnmatch.fnmatch(filename, '*.cc')
              or fnmatch.fnmatch(filename, '*.h')):
        continue
      file_path = os.path.abspath(os.path.join(dirpath, filename))
      logging.info("Procesing '%s'" % file_path)

      if source_filter:
        skipping = True
        for suffix in source_filter:
          if file_path.endswith(suffix):
            skipping = False
        if skipping:
          logging.info("  *SKIPPING*")
          continue

      file_coverable_lines = set()
      file_covered_lines = set()

      for filename in coverable_lines.iterkeys():
        if filename.endswith(file_path):
          file_coverable_lines = coverable_lines[filename]
      for filename in covered_lines.iterkeys():
        if filename.endswith(file_path):
          file_covered_lines = covered_lines[filename]

      total_coverable_count += len(file_coverable_lines)
      total_covered_count += len(file_covered_lines)

      if not file_coverable_lines:
        logging.info("  *NO COVERABLE LINES*")
      else:
        logging.info("  Coverage: %.2f%% [%d/%d]"
                     % (100.0*len(file_covered_lines)/len(file_coverable_lines),
                        len(file_covered_lines),
                        len(file_coverable_lines)))

      with open(file_path, "r") as source_file:
        with open("%s.%s" % (file_path, args.extension), "w") as gcov_file:
          line_counter = 1
          for source_line in source_file:
            if line_counter in file_covered_lines:
              line_info = "1"
            elif line_counter in file_coverable_lines:
              line_info = "#####"
            else:
              line_info = "-"

            print >>gcov_file, "%9s:%5d:%s" % (line_info,
                                               line_counter,
                                               source_line),
            line_counter += 1

      logging.info("----")

  logging.info("========")
  logging.info("TOTAL LINES:   %d" % total_coverable_count)
  logging.info("COVERED LINES: %d" % total_covered_count)
  if total_coverable_count:
    logging.info("COVERAGE:      %.2f%%" % (100.0 * total_covered_count / total_coverable_count))

if __name__ == "__main__":
  main()
