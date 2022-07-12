/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <sys/types.h>

#include <string>

#include "./fbbcomm.h"

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags);

/** Add two struct timespecs. Equivalent to the same method of BSD. */
#define timespecadd(a, b, res) do {             \
  (res)->tv_sec = (a)->tv_sec + (b)->tv_sec;    \
  (res)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec; \
  if ((res)->tv_nsec >= 1000 * 1000 * 1000) {   \
    (res)->tv_sec++;                            \
    (res)->tv_nsec -= 1000 * 1000 * 1000;       \
  }                                             \
} while (0)

/** Subtract two struct timespecs. Equivalent to the same method of BSD. */
#define timespecsub(a, b, res) do {             \
  (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
  (res)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
  if ((res)->tv_nsec < 0) {                     \
    (res)->tv_sec--;                            \
    (res)->tv_nsec += 1000 * 1000 * 1000;       \
  }                                             \
} while (0)

/** Compare two struct timespecs. Equivalent to the same method of BSD. */
#define timespeccmp(a, b, OP)                   \
  (((a)->tv_sec == (b)->tv_sec) ?               \
      (((a)->tv_nsec)OP((b)->tv_nsec)) :        \
       (((a)->tv_sec)OP((b)->tv_sec)))

/** Get the seek offset and fcntl flags of the given process's given fd. Linux-specific. */
bool get_fdinfo(pid_t pid, int fd, ssize_t *offset, int *flags);

namespace firebuild {

void ack_msg(const int conn, const uint16_t ack_num);
void send_fbb(int conn, int ack_num, const FBBCOMM_Builder *msg, int *fds = NULL, int fd_count = 0);

void fb_perror(const char *s);

}  /* namespace firebuild */
#endif  // FIREBUILD_UTILS_H_
