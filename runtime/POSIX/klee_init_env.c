//===-- klee_init_env.c ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/klee.h"
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include "fd.h"
#include "multiprocess.h"
#include "misc.h"
#include "sockets.h"
#include "symfs.h"
#include "netlink.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

static void __emit_error(const char *msg) {
  klee_report_error(__FILE__, __LINE__, msg, "user.err");
}

/* Helper function that converts a string to an integer, and
   terminates the program with an error message is the string is not a
   proper number */
static long int __str_to_int(char *s, const char *error_msg) {
  long int res;
  char *endptr;

  if (*s == '\0')
    __emit_error(error_msg);

  res = strtol(s, &endptr, 0);

  if (*endptr != '\0')
    __emit_error(error_msg);

  return res;
}

static int __isprint(const char c) {
  /* Assume ASCII */
  return ((32 <= c) & (c <= 126));
}

static int __streq(const char *a, const char *b) {
  while (*a == *b) {
    if (!*a)
      return 1;
    a++;
    b++;
  }
  return 0;
}

static char *__get_sym_str(int numChars, char *name) {
  int i;
  char *s = malloc(numChars+1);
  klee_mark_global(s);
  klee_make_symbolic(s, numChars+1, name);

  for (i=0; i<numChars; i++)
    klee_prefer_cex(s, __isprint(s[i]));

  s[numChars] = '\0';
  return s;
}

static void __add_arg(int *argc, char **argv, char *arg, int argcMax) {
  if (*argc==argcMax) {
    __emit_error("too many arguments for klee_init_env");
  } else {
    argv[*argc] = arg;
    (*argc)++;
  }
}

static void __add_symfs_file(fs_init_descriptor_t *fid,
    enum sym_file_type file_type, const char *file_path) {
  if (fid->n_sym_files >= MAX_FILES) {
    __emit_error("Maximum number of allowed symbolic files exceeded");
  }

  sym_file_descriptor_t *sfd = &fid->sym_files[fid->n_sym_files++];
  sfd->file_type = file_type;
  sfd->file_path = file_path;
}

void klee_init_env(int argc, char **argv) {
  fs_init_descriptor_t fid;
  fid.n_sym_files = 0;
  fid.allow_unsafe = 0;
  fid.overlapped_writes = 1;

  int k=0;
  const char* msg = "Invalid Klee initialization syntax.";

  // TODO(sbucur): Factor this into multiple functions
  while (k < argc) {
    if (__streq(argv[k], "--sym-files") || __streq(argv[k], "-sym-files")) {
      if (k+2 >= argc)
        __emit_error(msg);

      k++;
      int sym_files_count = __str_to_int(argv[k++], msg);
      int sym_file_size = __str_to_int(argv[k++], msg);

      int i;
      for (i = 0; i < sym_files_count; i++) {
        if (fid.n_sym_files >= MAX_FILES) {
          __emit_error("Maximum number of allowed symbolic files exceeded");
        }

        sym_file_descriptor_t *sfd = &fid.sym_files[fid.n_sym_files++];
        sfd->file_type = PURE_SYMBOLIC;
        sfd->file_size = sym_file_size;
      }
    } else if (__streq(argv[k], "--sym-file") || __streq(argv[k], "-sym-file")) {
      if (k+1 >= argc)
        __emit_error(msg);

      k++;
      __add_symfs_file(&fid, SYMBOLIC, argv[k++]);
    } else if (__streq(argv[k], "--con-file") || __streq(argv[k], "-con-file")) {
      if (k+1 >= argc)
        __emit_error(msg);

      k++;
      __add_symfs_file(&fid, CONCRETE, argv[k++]);
    } else if (__streq(argv[k], "--unsafe") || __streq(argv[k], "-unsafe")) {
      fid.allow_unsafe = 1;
      k++;
    } else if (__streq(argv[k], "--no-overlapped") || __streq(argv[k], "-no-overlapped")) {
      fid.overlapped_writes = 0;
      k++;
    } else {
      k++;
    }
  }

  klee_init_processes();
  klee_init_symfs(&fid);
  klee_init_fdt();
  klee_init_mmap();
  klee_init_network();
  klee_init_netlink();
}

void klee_process_args(int* argcPtr, char*** argvPtr) {
  int argc = *argcPtr;
  char** argv = *argvPtr;

  int new_argc = 0, n_args;
  char* new_argv[1024];
  unsigned max_len, min_argvs, max_argvs;
  char** final_argv;
  char sym_arg_name[5] = "arg";
  unsigned sym_arg_num = 0;
  int k=0, i;

  sym_arg_name[4] = '\0';

  while (k < argc) {
    if (__streq(argv[k], "--sym-arg") || __streq(argv[k], "-sym-arg")) {
      const char *msg = "--sym-arg expects an integer argument <max-len>";
      if (++k == argc)
        __emit_error(msg);

      max_len = __str_to_int(argv[k++], msg);
      sym_arg_name[3] = '0' + sym_arg_num++;
      __add_arg(&new_argc, new_argv,
                __get_sym_str(max_len, sym_arg_name),
                1024);
    } else if (__streq(argv[k], "--sym-args") || __streq(argv[k], "-sym-args")) {
      const char *msg =
        "--sym-args expects three integer arguments <min-argvs> <max-argvs> <max-len>";

      if (k+3 >= argc)
        __emit_error(msg);

      k++;
      min_argvs = __str_to_int(argv[k++], msg);
      max_argvs = __str_to_int(argv[k++], msg);
      max_len = __str_to_int(argv[k++], msg);

      n_args = klee_range(min_argvs, max_argvs+1, "n_args");
      for (i=0; i < n_args; i++) {
        sym_arg_name[3] = '0' + sym_arg_num++;
        __add_arg(&new_argc, new_argv,
                  __get_sym_str(max_len, sym_arg_name),
                  1024);
      }
    } else if (__streq(argv[k], "--sym-files") || __streq(argv[k], "-sym-files")) {
      k+=3;
    } else if (__streq(argv[k], "--sym-file") || __streq(argv[k], "-sym-file")) {
      k+=2;
    } else if (__streq(argv[k], "--con-file") || __streq(argv[k], "-con-file")) {
      k+=2;
    } else if (__streq(argv[k], "--unsafe") || __streq(argv[k], "-unsafe")) {
      k++;
    } else if (__streq(argv[k], "--no-overlapped") || __streq(argv[k], "-no-overlapped")) {
      k++;
    } else {
      /* simply copy arguments */
      __add_arg(&new_argc, new_argv, argv[k++], 1024);
    }
  }

  final_argv = (char**) malloc((new_argc+1) * sizeof(*final_argv));
  klee_mark_global(final_argv);
  memcpy(final_argv, new_argv, new_argc * sizeof(*final_argv));
  final_argv[new_argc] = 0;

  *argcPtr = new_argc;
  *argvPtr = final_argv;

  klee_event(__KLEE_EVENT_BREAKPOINT, __KLEE_BREAK_TRACE);
}
