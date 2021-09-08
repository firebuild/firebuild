/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef SHIM_FIREBUILD_SHIM_H_
#define SHIM_FIREBUILD_SHIM_H_

#ifdef __cplusplus
namespace firebuild {
#endif

/**
 * Format of the data part of the shim message.
 */
typedef struct shim_msg_ {
  /** PID of the shim, which will also be used by the first exec child. */
  int pid;
  /**
   * Number of open fds inherited by the shim. The control message part contains one extra fd
   * aftern the inherited ones that should be closed by the supervisor to signal the consumption
   * of the message. */
  int fd_count;
  /**
   * Extra information about the fds sent in the control message in the same order.
   * The fds are grouped by using the same inode and ordered by access modes
   * (and then their fd number). The same inode groups are separated by ':'-s, fds sharing
   * the same inode are separated by ',', and mode is listed after each fd number after '='.
   * e.g. "0=0:1=1,2=1"
   */
  char fd_map[0];
} shim_msg_t;

#ifdef __cplusplus
}  // namespace firebuild
#endif


#endif  /* SHIM_FIREBUILD_SHIM_H_ */

