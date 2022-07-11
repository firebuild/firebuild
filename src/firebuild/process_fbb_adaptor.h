/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_FBB_ADAPTOR_H_
#define FIREBUILD_PROCESS_FBB_ADAPTOR_H_

#include "./fbbcomm.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild  {
  /**
   * Converts messages from monitored processes to calls to Process instances.
   * It is not a clean implementation of the GoF Adaptor pattern, but something
   * like that. The class itself is never instantiated, but groups a set of
   * static functions which accept a Process reference and an incoming FBB
   * message for the process.
   */
class ProcessFBBAdaptor {
 public:
  static int handle(Process *proc, const FBBCOMM_Serialized_pre_open *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_open *msg, int fd_conn, int ack_num);
  static int handle(Process *proc, const FBBCOMM_Serialized_freopen *msg, int fd_conn, int ack_num);
  static int handle(Process *proc, const FBBCOMM_Serialized_dlopen *msg, int fd_conn, int ack_num);
  static int handle(Process *proc, const FBBCOMM_Serialized_close *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_closefrom *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_close_range *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_unlink *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_rmdir *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_mkdir *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_fstatat *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_faccessat *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_fchmodat *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_memfd_create *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_timerfd_create *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_epoll_create *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_eventfd *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_signalfd *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_dup *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_dup3 *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_rename *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_symlink *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_fcntl *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_ioctl *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_read_from_inherited *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_write_to_inherited *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_seek_in_inherited *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_umask *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_chdir *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_fchdir *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_pipe_request *msg, int fd_conn);
  static int handle(Process *proc, const FBBCOMM_Serialized_pipe_fds *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_socket *msg);
  static int handle(Process *proc, const FBBCOMM_Serialized_socketpair *msg);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessFBBAdaptor);
};

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_FBB_ADAPTOR_H_
