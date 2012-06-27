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

"""Reads .gcov files and displays a summary report for each file."""

__author__ = "sbucur@google.com (Stefan Bucur)"

import argparse
import fnmatch
import logging
import os


class GCovParser(object):
  """GCov file parser."""

  def __init__(self, source_filter):
    self.source_filter = source_filter

  def ProcessGCovFile(self, gcov_file_name):
    """Processes a .gcov file and logs statistics about it."""

    logging.info("Processing '%s'" % gcov_file_name)
    file_source = None
    skipping = False

    covered_lines = 0
    coverable_lines = 0
    with open(gcov_file_name, "r") as f:
      for line in f:
        line = line.strip()

        cov_info, line_no, line_data = line.split(":", 2)
        cov_info = cov_info.strip()
        line_no = int(line_no.strip())

        if line_no == 0:
          key, value = line_data.split(":", 1)
          if key == "Source":
            file_source = value
            logging.info("  Source: %s" % file_source)
            if self.source_filter:
              skipping = True
              for suffix in self.source_filter:
                if file_source.endswith(suffix):
                  skipping = False
              if skipping:
                logging.info("  *SKIPPING*")
                break
          elif key == "Programs":
            logging.info("  Number of runs: %d" % int(value))
        else:
          if cov_info == "-":  # Uncoverable line
            pass
          elif cov_info == "#####":  # Coverable, but uncovered line
            coverable_lines += 1
          else:  # Covered line
            coverable_lines += 1
            covered_lines += 1

    if not skipping:
      if not coverable_lines:
        logging.info("  *NO COVERABLE LINES*")
      else:
        logging.info("  Coverage: %.2f%% [%d/%d]"
                     % (100.0*covered_lines/coverable_lines,
                        covered_lines,
                        coverable_lines))

    logging.info("----")

    return coverable_lines, covered_lines


def main():
  logging.basicConfig(format="[%(levelname)s] %(message)s", level=logging.INFO)

  parser = argparse.ArgumentParser(description="Compute GCOV code coverage.")
  parser.add_argument("code_root", default=".",
                      help="Where to look for .gcov files.")
  parser.add_argument("-c", "--coverable", help="A .coverable file")

  args = parser.parse_args()

  if args.coverable:
    with open(args.coverable, "r") as cov_file:
      source_filter = [line.strip() for line in cov_file.readlines()]
  else:
    source_filter = None

  total_covered_lines = 0
  total_coverable_lines = 0

  gcov_parser = GCovParser(source_filter)

  for dirpath, _, filenames in os.walk(args.code_root):
    for filename in fnmatch.filter(filenames, '*.gcov'):
      coverable_lines, covered_lines = gcov_parser.ProcessGCovFile(
          os.path.join(dirpath, filename))

      total_coverable_lines += coverable_lines
      total_covered_lines += covered_lines

  logging.info("========")
  logging.info("TOTAL LINES:   %d" % total_coverable_lines)
  logging.info("COVERED LINES: %d" % total_covered_lines)
  if total_coverable_lines:
    logging.info("COVERAGE:      %.2f%%" % (100.0 * total_covered_lines / total_coverable_lines))

if __name__ == "__main__":
  main()
