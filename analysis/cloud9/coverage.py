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

"""Coverage log manipulation."""

__author__ = "sbucur@google.com (Stefan Bucur)"

import fnmatch
import logging
import os
import shutil
import struct
import sys

import jinja2

import cloud9.CoverageLogs_pb2 as CoverageLogs

################################################################################
# Annotation data - Final product passed to the template rendering engine
################################################################################

class AnnotatedSourceFile(object):
  def __init__(self, source_stats, lines):
    self.source_stats = source_stats
    self.lines = lines
    
    
class SourceFileStatistics(object):
  def __init__(self, file_name, coverage, covered_line_count, coverable_line_count):
    self.file_name = file_name
    self.coverage = coverage
    self.covered_line_count = covered_line_count
    self.coverable_line_count = coverable_line_count
    
    
class AnnotatedSourceLine(object):
  def __init__(self, number, contents, coverable, covered):
    self.number = number
    self.contents = contents
    self.coverable = coverable
    self.covered = covered


################################################################################
# Protobuf Parsing
################################################################################


def _ReadCoverageInfoEntry(data_file):
  """Reads a packet of data from the specified file."""

  UINT32_SIZE = 4

  pkt_size_buf = data_file.read(UINT32_SIZE)
  if len(pkt_size_buf) != UINT32_SIZE:
    raise ValueError("Invalid packet size read.")

  pkt_size = struct.unpack("I", pkt_size_buf)[0]

  pkt = data_file.read(pkt_size)

  if len(pkt) != pkt_size:
    raise ValueError("Incomplete packet.")

  return pkt


def _ReadCoverageInfoMessage(message):
  """Parses a coverage protobuf message."""

  cov_info = {}

  for file_cov_msg in message.file_coverage:
    file_path = os.path.abspath(file_cov_msg.file_name)
    cov_info[file_path] = set(file_cov_msg.covered_lines)

  return cov_info


def ReadLastCoverageInfo(data_file):
  """Reads the last set of coverage values from the specified file."""

  try:
    header_pkt = _ReadCoverageInfoEntry(data_file)
  except ValueError as e:
    logging.error("Cannot read header packet: %s" % e)
    sys.exit(1)

  coverable_lines_msg = CoverageLogs.CoverageInfo()
  coverable_lines_msg.ParseFromString(header_pkt)

  last_coverage_pkt = None
  while True:
    try:
      last_coverage_pkt = _ReadCoverageInfoEntry(data_file)
    except ValueError:
      break
  if not last_coverage_pkt:
    logging.error("Cannot read coverage packet.")
    sys.exit(1)

  covered_lines_msg = CoverageLogs.CoverageInfo()
  covered_lines_msg.ParseFromString(last_coverage_pkt)

  coverable_lines = _ReadCoverageInfoMessage(coverable_lines_msg)
  covered_lines = _ReadCoverageInfoMessage(covered_lines_msg)

  return coverable_lines, covered_lines

################################################################################


class SourceFilter(object):
  """Filter object based on a .coverable file."""
  
  def __init__(self, file_list):
    self.file_list = file_list

  def IsValid(self, file_name):
    """Returns True if file_name passes the filter."""
     
    for suffix in self.file_list:
      if file_name.endswith(suffix):
        return True
    return False
  
  
def LoadSourceFilter(coverable_file_name):
  """Loads a SourceFilter from a specified file."""
  
  with open(coverable_file_name, "r") as cov_file:
    file_list = [line.strip() for line in cov_file.readlines()]
    return SourceFilter(file_list)
  

################################################################################
  

class CoverageAnnotator(object):
  """Produces coverage annotation."""
  
  def __init__(self, coverage_data=None, ground_coverage_data=None, source_filter=None):
    self.source_filter = source_filter
    self.coverage_data = coverage_data
    self.ground_coverage_data = ground_coverage_data
    
    # Statistics
    self._total_coverable_count = 0
    self._total_covered_count = 0
    
  def AnnotateSourceFile(self, file_name):
    """Annotates source file based on available coverage information."""
    
    coverable_lines = set()
    covered_lines = set()
    
    # Search for the available coverage information
    
    for candidate_name in self.ground_coverage_data.iterkeys():
      if candidate_name.endswith(file_name):
        coverable_lines = self.ground_coverage_data[candidate_name]
        break
      
    for candidate_name in self.coverage_data.iterkeys():
      if candidate_name.endswith(file_name):
        covered_lines = self.coverage_data[candidate_name]
        
    # Generate statistics

    self._total_coverable_count += len(coverable_lines)
    self._total_covered_count += len(covered_lines)

    source_stats = SourceFileStatistics(
        file_name, 100.0*len(covered_lines)/len(coverable_lines)
        if coverable_lines else 0.0,
        len(covered_lines),
        len(coverable_lines))
    
    # Generate per-line annotation

    annotated_file = AnnotatedSourceFile(source_stats, [])

    with open(file_name, "r") as source_file:
      line_counter = 1
      for source_line in source_file:
        annotated_line = AnnotatedSourceLine(
            line_counter, source_line.rstrip(),
            line_counter in coverable_lines,
            line_counter in covered_lines)
        annotated_file.lines.append(annotated_line)
        line_counter += 1
        
    return annotated_file
  
  def AnnotateDirectory(self, root_path):
    """Annotate all source files under the given directory."""
    
    annotation_data = []
    
    for dirpath, _, filenames in os.walk(root_path):
      for filename in filenames:
        if not self._DefaultFileNameFilter(filename):
          continue
  
        file_path = os.path.abspath(os.path.join(dirpath, filename))
        logging.info("Processing '%s'" % file_path)
  
        if self.source_filter and not self.source_filter.IsValid(file_path):
          logging.info("  *SKIPPING*")
          continue
        
        annotated_file = self.AnnotateSourceFile(file_path)
        annotation_data.append(annotated_file)

    return annotation_data
    
  @staticmethod
  def _DefaultFileNameFilter(file_name):
    return (fnmatch.fnmatch(file_name, '*.cc') 
            or fnmatch.fnmatch(file_name, '*.h'))
    
    
################################################################################

class AnnotationRenderer(object):
  def __init__(self):  
    self.base_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
    
    # Construct the HTML template
    self.jinja_env = jinja2.Environment(
        loader=jinja2.PackageLoader("cloud9", "views"))
  
    self.report_template = self.jinja_env.get_template("report.html")
    self.source_template = self.jinja_env.get_template("source-list.html")
    
  @staticmethod
  def _CreateUniqueDirectory(output_path, write_sym_link=True):
    sym_link = None
    unique_path = output_path
    if os.path.exists(unique_path):
      counter = 0
      while os.path.exists(unique_path):
        counter += 1
        unique_path = "%s-%d" % (output_path, counter)
      sym_link = "%s-last" % output_path
  
    os.mkdir(unique_path)
  
    if sym_link and write_sym_link:
      if os.path.islink(sym_link):
        os.unlink(sym_link)
  
      if not os.path.exists(sym_link):
        os.symlink(unique_path, sym_link)
        
    return unique_path
    
  def RenderAnnotation(self, annotation_data, output_path):
    """Renders HTML report of specified coverage annotation.
    
    The current directory is changed in the report.
    """
    
    output_path = self._CreateUniqueDirectory(output_path)
    os.chdir(output_path)
  
    with open("report.html", "w") as report_file:
      report_file.write(
          self.report_template.render(annotation_data=annotation_data))
  
    os.mkdir("src")
  
    for annotated_file in annotation_data:
      with open("src/%s.html" % annotated_file.source_stats.file_name.replace("/", "_"), "w") as source_file:
        source_file.write(self.source_template.render(source_stats=annotated_file.source_stats, lines=annotated_file.lines))
  
    for static in ["cloud9/css/source.css", "cloud9/css/report.css"]:
      shutil.copy(os.path.join(self.base_dir, static), ".")
