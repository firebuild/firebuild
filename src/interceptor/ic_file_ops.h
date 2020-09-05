/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_IC_FILE_OPS_H_
#define FIREBUILD_IC_FILE_OPS_H_

#include <link.h>
#include <dirent.h>
#include <stdio.h>

#include "interceptor/intercept.h"
#include "interceptor/interceptors.h"

int intercept_fopen_mode_to_open_flags_helper(const char * mode);
void clear_file_state(const int fd);
void clear_all_file_states();
void copy_file_state(const int to_fd, const int from_fd);
extern int firebuild_fake_main(int argc, char **argv, char **env);

/* Same as fileno(), but with safe NULL pointer handling. */
static inline int safe_fileno(FILE *stream) {
  int ret = stream ? ic_orig_fileno(stream) : -1;
  if (ret == fb_sv_conn) {
    assert(0 && "fileno() returned the connection fd");
  }
  return ret;
}

/* Same as dirfd(), but with safe NULL pointer handling. */
static inline int safe_dirfd(DIR *dirp) {
  int ret = dirp ? ic_orig_dirfd(dirp) : -1;
  if (ret == fb_sv_conn) {
    assert(0 && "dirfd() returned the connection fd");
  }
  return ret;
}

#endif  // FIREBUILD_IC_FILE_OPS_H_
