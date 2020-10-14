/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE

#include "interceptor/ic_file_ops.h"

#include <fcntl.h>
#include <mntent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <link.h>
#include <sys/resource.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "interceptor/intercept.h"

int intercept_fopen_mode_to_open_flags_helper(const char * mode) {
  int flags;
  const char * p = mode;
  /* invalid mode , NULL will crash fopen() anyway */
  if (p == NULL) {
    return -1;
  }

  /* open() flags */
  switch (*p) {
    case 'r': {
      p++;
      if (*p != '+') {
        flags = O_RDONLY;
      } else {
        p++;
        flags = O_RDWR;
      }
      break;
    }
    case 'w': {
      p++;
      if (*p != '+') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
      } else {
        p++;
        flags = O_RDWR | O_CREAT | O_TRUNC;
      }
      break;
    }
    case 'a': {
      p++;
      if (*p != '+') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
      } else {
        p++;
        flags = O_RDWR | O_CREAT | O_APPEND;
      }
      break;
    }
    default:
      /* would cause EINVAL in open()*/
      return -1;
  }

  /* glibc extensions */
  while (*p != '\0') {
    switch (*p) {
      case 'b':
        /* ignore, not interesting from interception POV */
        p++;
        continue;
      case 'c': {
        /* ignore, not interesting from interception POV */
        p++;
        continue;
      }
      case 'e': {
        flags |= O_CLOEXEC;
        break;
      }
      case 'm': {
        /* ignore, not interesting from interception POV */
        p++;
        continue;
      }
      case 'x': {
        flags |= O_EXCL;
        break;
      }
      case ',': {
        /* ,ccs=string is not interesting from intercepion POV */
        return flags;
      }
      default:
        /* */
        break;
    }
    p++;
  }

  return flags;
}

void clear_file_state(const int fd) {
  if (fd >= 0 && fd < IC_FD_STATES_SIZE) {
    ic_fd_states[fd].read = false;
    ic_fd_states[fd].written = false;
  }
}

void clear_all_file_states() {
  for (int fd = 0; fd < IC_FD_STATES_SIZE; fd++) {
    ic_fd_states[fd].read = false;
    ic_fd_states[fd].written = false;
  }
}

void copy_file_state(const int to_fd, const int from_fd) {
  if ((to_fd >= 0) && (to_fd < IC_FD_STATES_SIZE) &&
      (from_fd >= 0) && (from_fd < IC_FD_STATES_SIZE)) {
    ic_fd_states[to_fd] = ic_fd_states[from_fd];
  }
}
