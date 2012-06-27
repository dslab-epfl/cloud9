/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */

#ifndef CLOUD9_POSIX_SYMFS_H_
#define CLOUD9_POSIX_SYMFS_H_

#include <sys/types.h>

#include "config.h"

struct disk_file;
struct stat;

typedef struct {
  int (*truncate)(struct disk_file *dfile, size_t size);
  ssize_t (*read)(struct disk_file *dfile, void *buf, size_t count, off_t offset);
  ssize_t (*write)(struct disk_file *dfile, const void *buf, size_t count, off_t offset);
} disk_file_ops_t;

typedef struct disk_file {
  char *name;
  struct stat *stat;

  disk_file_ops_t ops;
  char contents[0];
} disk_file_t;  // The "disk" storage of the file

typedef struct {
  unsigned count;
  disk_file_t **files;
  disk_file_t *stdin_file;

  char allow_unsafe;
  char overlapped_writes;
} filesystem_t;

enum sym_file_type {
  PURE_SYMBOLIC = 0,
  SYMBOLIC = 2,
  CONCRETE = 3
};

typedef struct {
  union {
    int file_size;
    const char *file_path;
  };
  enum sym_file_type file_type;
} sym_file_descriptor_t;

typedef struct {
  int n_sym_files;
  sym_file_descriptor_t sym_files[MAX_FILES];
  char allow_unsafe;
  char overlapped_writes;
} fs_init_descriptor_t;

extern filesystem_t _fs;

disk_file_t *__get_sym_file(const char *pathname);

void klee_init_symfs(fs_init_descriptor_t *fid);

#endif  //CLOUD9_POSIX_SYMFS_H_
