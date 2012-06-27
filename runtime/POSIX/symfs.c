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

#include "symfs.h"

#include <klee/klee.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "buffers.h"
#include "models.h"

filesystem_t _fs;

////////////////////////////////////////////////////////////////////////////////
// Symbolic Files Operations
////////////////////////////////////////////////////////////////////////////////

static ssize_t _read_symbolic(struct disk_file *dfile, void *buf, size_t count,
                              off_t offset) {
  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];
  return _block_read(buff, buf, count, offset);
}

static ssize_t _write_symbolic(struct disk_file *dfile, const void *buf,
                               size_t count, off_t offset) {
  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];
  ssize_t result = _block_write(buff, buf, count, offset);
  dfile->stat->st_size = buff->size;
  return result;
}

static int _truncate_symbolic(struct disk_file *dfile, size_t size) {
  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];

  if (size > buff->max_size)
    return -1;

  buff->size = size;
  dfile->stat->st_size = size;

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// FS Routines
////////////////////////////////////////////////////////////////////////////////

/* Returns pointer to the symbolic file structure if the pathname is symbolic */
disk_file_t *__get_sym_file(const char *pathname) {
  char c = pathname[0];

  if (c == 0 || pathname[1] != 0)
    return NULL;

  unsigned int i;
  for (i = 0; i < _fs.count; i++) {
    if (c == 'A' + (char)i) {
      disk_file_t *df = _fs.files[i];
      return df;
    }
  }

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// FS Initialization
////////////////////////////////////////////////////////////////////////////////

static int __isupper(const char c) {
  return (('A' <= c) & (c <= 'Z'));
}

static void _fill_stats_field(disk_file_t *dfile, const struct stat *defstats) {
  struct stat *stat = dfile->stat;

  /* Important since we copy this out through getdents, and readdir
     will otherwise skip this entry. For same reason need to make sure
     it fits in low bits. */
  klee_assume((stat->st_ino & 0x7FFFFFFF) != 0);

  /* uclibc opendir uses this as its buffer size, try to keep
     reasonable. */
  klee_assume((stat->st_blksize & ~0xFFFF) == 0);

  klee_prefer_cex(stat, !(stat->st_mode & ~(S_IFMT | 0777)));
  klee_prefer_cex(stat, stat->st_dev == defstats->st_dev);
  klee_prefer_cex(stat, stat->st_rdev == defstats->st_rdev);
  klee_prefer_cex(stat, (stat->st_mode&0700) == 0600);
  klee_prefer_cex(stat, (stat->st_mode&0070) == 0020);
  klee_prefer_cex(stat, (stat->st_mode&0007) == 0002);
  klee_prefer_cex(stat, (stat->st_mode&S_IFMT) == S_IFREG);
  klee_prefer_cex(stat, stat->st_nlink == 1);
  klee_prefer_cex(stat, stat->st_uid == defstats->st_uid);
  klee_prefer_cex(stat, stat->st_gid == defstats->st_gid);
  klee_prefer_cex(stat, stat->st_blksize == 4096);
  klee_prefer_cex(stat, stat->st_atime == defstats->st_atime);
  klee_prefer_cex(stat, stat->st_mtime == defstats->st_mtime);
  klee_prefer_cex(stat, stat->st_ctime == defstats->st_ctime);
  stat->st_size = 0;
  stat->st_blocks = 8;
}

static void _init_stats(disk_file_t *dfile, const char *symname,
    const struct stat *defstats, int symstats) {
  static char namebuf[64];

  dfile->stat = (struct stat*)malloc(sizeof(struct stat));

  if (symstats) {
    strcpy(namebuf, symname);
    strcat(namebuf, "-stat");
    klee_make_symbolic(dfile->stat, sizeof(struct stat), namebuf);
    klee_make_shared(dfile->stat, sizeof(struct stat));
    _fill_stats_field(dfile, defstats);
  } else {
    memcpy(dfile->stat, defstats, sizeof(struct stat));
  }
}

static void _init_file_name(disk_file_t *dfile, const char *symname) {
  static char namebuf[64];
  dfile->name = (char*)malloc(MAX_PATH_LEN);

  strcpy(namebuf, symname);
  strcat(namebuf, "-name");
  klee_make_symbolic(dfile->name, MAX_PATH_LEN, namebuf);
  klee_make_shared(dfile->name, MAX_PATH_LEN);

  unsigned int i;
  for (i = 0; i < MAX_PATH_LEN; i++) {
    klee_prefer_cex(dfile->name, __isupper(dfile->name[i]));
  }
}

static size_t _read_file_contents(const char *file_name, size_t size, char *orig_contents) {
  int orig_fd = CALL_UNDERLYING(open, file_name, O_RDONLY);
  assert(orig_fd >= 0 && "Could not open original file.");

  size_t current_size = 0;
  ssize_t bytes_read = 0;
  while ((bytes_read = CALL_UNDERLYING(
      read, orig_fd, orig_contents + current_size, size - current_size))) {
    if (bytes_read < 0) {
      klee_warning("Error while reading original file.");
      break;
    }
    current_size += bytes_read;
    if (current_size == size) {
      break;
    }
  }

  CALL_UNDERLYING(close, orig_fd);

  return current_size;
}

static void _init_pure_symbolic_buffer(disk_file_t *dfile, size_t maxsize,
    const char *symname) {
  static char namebuf[64];

  // Initializing the buffer contents...
  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];
  _block_init(buff, maxsize);
  buff->size = maxsize;

  strcpy(namebuf, symname);
  strcat(namebuf, "-data");
  klee_make_symbolic(buff->contents, maxsize, namebuf);
  klee_make_shared(buff->contents, maxsize);
}

static void _init_dual_buffer(disk_file_t *dfile, const char *origname,
    size_t size, const char *symname, int make_symbolic) {

  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];
  _block_init(buff, size);
  buff->size = size;

  if (make_symbolic) {
    static char namebuf[64];
    strcpy(namebuf, symname);
    strcat(namebuf, "-data");

    klee_make_symbolic(buff->contents, size, namebuf);
    klee_make_shared(buff->contents, size);
  } else {
    _read_file_contents(origname, size, buff->contents);
  }
}

static disk_file_t *_create_dual_file(const char *origname,
    const char *symname, int make_symbolic) {
  disk_file_t *dfile = (disk_file_t*)malloc(
        sizeof(disk_file_t) + sizeof(block_buffer_t));
  klee_make_shared(dfile, sizeof(disk_file_t) + sizeof(block_buffer_t));

  struct stat s;
  int res = CALL_UNDERLYING(stat, origname, &s);
  assert(res == 0 && "Could not get the stat of the original file.");

  _init_file_name(dfile, symname);
  _init_dual_buffer(dfile, origname, s.st_size, symname, make_symbolic);

  dfile->stat = (struct stat*)malloc(sizeof(struct stat));
  memcpy(dfile->stat, &s, sizeof(struct stat));

  // Register the operations
  memset(&dfile->ops, 0, sizeof(dfile->ops));
  dfile->ops.read = _read_symbolic;
  dfile->ops.write = _write_symbolic;
  dfile->ops.truncate = _truncate_symbolic;

  return dfile;
}

static disk_file_t *_create_pure_symbolic_file(size_t maxsize, const char *symname,
    const struct stat *defstats, int symstats) {
  disk_file_t *dfile = (disk_file_t*)malloc(
      sizeof(disk_file_t) + sizeof(block_buffer_t));
  klee_make_shared(dfile, sizeof(disk_file_t) + sizeof(block_buffer_t));

  _init_file_name(dfile, symname);
  _init_pure_symbolic_buffer(dfile, maxsize, symname);
  _init_stats(dfile, symname, defstats, symstats);

  // Update the stat size
  block_buffer_t *buff = (block_buffer_t*)&dfile->contents[0];
  dfile->stat->st_size = buff->size;

  // Register the operations
  memset(&dfile->ops, 0, sizeof(dfile->ops));
  dfile->ops.read = _read_symbolic;
  dfile->ops.write = _write_symbolic;
  dfile->ops.truncate = _truncate_symbolic;

  return dfile;
}

void klee_init_symfs(fs_init_descriptor_t *fid) {
  char fname[] = "FILE??";
  unsigned int fname_len = strlen(fname);

  struct stat s;
  int res = CALL_UNDERLYING(stat, ".", &s);

  assert(res == 0 && "Could not get default stat values");

  klee_make_shared(&_fs, sizeof(filesystem_t));
  memset(&_fs, 0, sizeof(filesystem_t));

  // Setting the unsafe (allow external writes) flag
  _fs.allow_unsafe = fid->allow_unsafe;
  // Setting the overlapped (keep per-state concrete file offsets) flag
  _fs.overlapped_writes = fid->overlapped_writes;

  _fs.count = fid->n_sym_files;
  _fs.files = (disk_file_t**)malloc(fid->n_sym_files*sizeof(disk_file_t*));
  klee_make_shared(_fs.files, fid->n_sym_files*sizeof(disk_file_t*));

  // Create n symbolic files
  int i;
  for (i = 0; i < fid->n_sym_files; i++) {
    fname[fname_len-1] = '0' + (i % 10);
    fname[fname_len-2] = '0' + (i / 10);

    switch (fid->sym_files[i].file_type) {
    case PURE_SYMBOLIC:
      _fs.files[i] = _create_pure_symbolic_file(fid->sym_files[i].file_size, fname, &s, 1);
      break;
    case SYMBOLIC:
      _fs.files[i] = _create_dual_file(fid->sym_files[i].file_path, fname, 1);
      break;
    case CONCRETE:
      _fs.files[i] = _create_dual_file(fid->sym_files[i].file_path, fname, 0);
      break;
    }
  }

  // Create the stdin symbolic file
  _fs.stdin_file = _create_pure_symbolic_file(MAX_STDINSIZE, "STDIN", &s, 1);
}
