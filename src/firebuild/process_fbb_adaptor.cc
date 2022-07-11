/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_fbb_adaptor.h"

#include <string>

#include "firebuild/execed_process.h"

namespace firebuild {
int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_pre_open *msg) {
  return proc->handle_pre_open(msg->get_dirfd_with_fallback(AT_FDCWD),
                               msg->get_pathname(), msg->get_pathname_len());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_open *msg, int fd_conn,
                             const int ack_num) {
  const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
  int error = msg->get_error_no_with_fallback(0);
  int ret = msg->get_ret_with_fallback(-1);
  return proc->handle_open(dirfd, msg->get_pathname(), msg->get_pathname_len(), msg->get_flags(),
                           msg->get_mode_with_fallback(0), ret, error, fd_conn, ack_num,
                           msg->get_pre_open_sent());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_freopen *msg, int fd_conn,
                          const int ack_num) {
  int error = msg->get_error_no_with_fallback(0);
  int oldfd = msg->get_ret_with_fallback(-1);
  int ret = msg->get_ret_with_fallback(-1);
  return proc->handle_freopen(msg->get_pathname(), msg->get_pathname_len(), msg->get_flags(),
                              oldfd, ret, error, fd_conn, ack_num, msg->get_pre_open_sent());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dlopen *msg, int fd_conn,
                             const int ack_num) {
  if (!msg->has_error_string() && msg->has_absolute_filename()) {
    return proc->handle_open(AT_FDCWD,
                             msg->get_absolute_filename(), msg->get_absolute_filename_len(),
                             O_RDONLY, 0, -1, 0, fd_conn, ack_num);
  } else {
    std::string filename = msg->has_absolute_filename() ? msg->get_absolute_filename() : "NULL";
    proc->exec_point()->disable_shortcutting_bubble_up("Process failed to dlopen() ", filename);
    return 0;
  }
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_close *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_close(msg->get_fd(), error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_closefrom *msg) {
  return proc->handle_closefrom(msg->get_lowfd());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_close_range *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_close_range(msg->get_first(), msg->get_last(), msg->get_flags(), error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_unlink *msg) {
  const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
  const int flags = msg->get_flags_with_fallback(0);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_unlink(dirfd, msg->get_pathname(), msg->get_pathname_len(), flags, error,
                             msg->get_pre_open_sent());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_rmdir *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_rmdir(msg->get_pathname(), msg->get_pathname_len(), error,
                            msg->get_pre_open_sent());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_mkdir *msg) {
  const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_mkdir(dirfd, msg->get_pathname(), msg->get_pathname_len(), error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fstatat *msg) {
  const int fd = msg->get_fd_with_fallback(AT_FDCWD);
  const mode_t st_mode = msg->get_st_mode_with_fallback(0);
  const off_t st_size = msg->get_st_size_with_fallback(0);
  const int flags = msg->get_flags_with_fallback(0);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_fstatat(fd, msg->get_pathname(), msg->get_pathname_len(),
                              flags, st_mode, st_size, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_faccessat *msg) {
  const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
  const int mode = msg->get_mode();
  const int flags = msg->get_flags_with_fallback(0);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_faccessat(dirfd, msg->get_pathname(), msg->get_pathname_len(),
                                mode, flags, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fchmodat *msg) {
  const int fd = msg->get_fd_with_fallback(AT_FDCWD);
  const mode_t mode = msg->get_mode();
  const int flags = msg->get_flags_with_fallback(0);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_fchmodat(fd, msg->get_pathname(), msg->get_pathname_len(),
                               mode, flags, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_memfd_create *msg) {
  return proc->handle_memfd_create(msg->get_flags(), msg->get_ret());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_timerfd_create *msg) {
  return proc->handle_timerfd_create(msg->get_flags(), msg->get_ret());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_epoll_create *msg) {
  return proc->handle_epoll_create(msg->get_flags_with_fallback(0), msg->get_ret());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_eventfd *msg) {
  return proc->handle_eventfd(msg->get_flags(), msg->get_ret());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_signalfd *msg) {
  return proc->handle_signalfd(msg->get_fd(), msg->get_flags(), msg->get_ret());
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dup3 *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  const int flags = msg->get_flags_with_fallback(0);
  return proc->handle_dup3(msg->get_oldfd(), msg->get_newfd(), flags, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dup *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_dup3(msg->get_oldfd(), msg->get_ret(), 0, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_rename *msg) {
  const int olddirfd = msg->get_olddirfd_with_fallback(AT_FDCWD);
  const int newdirfd = msg->get_newdirfd_with_fallback(AT_FDCWD);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_rename(olddirfd, msg->get_oldpath(), msg->get_oldpath_len(),
                             newdirfd, msg->get_newpath(), msg->get_newpath_len(), error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_symlink *msg) {
  const int newdirfd = msg->get_newdirfd_with_fallback(AT_FDCWD);
  const int error = msg->get_error_no_with_fallback(0);
  return proc->handle_symlink(msg->get_target(), newdirfd, msg->get_newpath(), error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fcntl *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  int arg = msg->get_arg_with_fallback(0);
  int ret = msg->get_ret_with_fallback(-1);
  return proc->handle_fcntl(msg->get_fd(), msg->get_cmd(), arg, ret, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_ioctl *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  int ret = msg->get_ret_with_fallback(-1);
  return proc->handle_ioctl(msg->get_fd(), msg->get_cmd(), ret, error);
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_read_from_inherited *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  if (error == 0) {
    proc->handle_read_from_inherited(msg->get_fd(), msg->get_is_pread());
  }
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_write_to_inherited *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  if (error == 0) {
    proc->handle_write_to_inherited(msg->get_fd(), msg->get_is_pwrite());
  }
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_seek_in_inherited *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  if (error == 0) {
    proc->handle_seek_in_inherited(msg->get_fd(), msg->get_modify_offset());
  }
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_umask *msg) {
  const mode_t old_umask = msg->get_ret();
  const mode_t new_umask = msg->get_mask();
  proc->handle_umask(old_umask, new_umask);
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_chdir *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  if (error == 0) {
    proc->handle_set_wd(msg->get_pathname(), msg->get_pathname_len());
  } else {
    proc->handle_fail_wd(msg->get_pathname());
  }
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fchdir *msg) {
  const int error = msg->get_error_no_with_fallback(0);
  if (error == 0) {
    proc->handle_set_fwd(msg->get_fd());
  }
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_pipe_request *msg,
                             int fd_conn) {
  const int flags = msg->get_flags_with_fallback(0);
  proc->handle_pipe_request(flags, fd_conn);
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_pipe_fds *msg) {
  const int fd0 = msg->get_fd0();
  const int fd1 = msg->get_fd1();
  proc->handle_pipe_fds(fd0, fd1);
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_socket *msg) {
  proc->handle_socket(msg->get_domain(), msg->get_type(), msg->get_protocol(),
                      msg->get_ret_with_fallback(-1), msg->get_error_no_with_fallback(0));
  return 0;
}

int ProcessFBBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_socketpair *msg) {
  proc->handle_socketpair(msg->get_domain(), msg->get_type(), msg->get_protocol(),
                          msg->get_fd0_with_fallback(-1), msg->get_fd1_with_fallback(-1),
                          msg->get_error_no_with_fallback(0));
  return 0;
}

}  /* namespace firebuild */
