/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_proto_adaptor.h"

#include <string>

#include "firebuild/execed_process.h"

namespace firebuild {
int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_open *msg, int fd_conn,
                             const int ack_num) {
  const int dirfd = fbbcomm_serialized_open_get_dirfd_with_fallback(msg, AT_FDCWD);
  int error = fbbcomm_serialized_open_get_error_no_with_fallback(msg, 0);
  int ret = fbbcomm_serialized_open_get_ret_with_fallback(msg, -1);
  return proc->handle_open(dirfd, fbbcomm_serialized_open_get_file(msg),
                           fbbcomm_serialized_open_get_file_len(msg),
                           fbbcomm_serialized_open_get_flags(msg), ret, error, fd_conn, ack_num);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_freopen *msg, int fd_conn,
                          const int ack_num) {
  int error = fbbcomm_serialized_freopen_get_error_no_with_fallback(msg, 0);
  int oldfd = fbbcomm_serialized_freopen_get_ret_with_fallback(msg, -1);
  int ret = fbbcomm_serialized_freopen_get_ret_with_fallback(msg, -1);
  return proc->handle_freopen(fbbcomm_serialized_freopen_get_file(msg),
                              fbbcomm_serialized_freopen_get_file_len(msg),
                              fbbcomm_serialized_freopen_get_flags(msg),
                              oldfd, ret, error, fd_conn, ack_num);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dlopen *msg, int fd_conn,
                             const int ack_num) {
  if (!fbbcomm_serialized_dlopen_has_error_string(msg) &&
      fbbcomm_serialized_dlopen_has_absolute_filename(msg)) {
    return proc->handle_open(AT_FDCWD, fbbcomm_serialized_dlopen_get_absolute_filename(msg),
                             fbbcomm_serialized_dlopen_get_absolute_filename_len(msg),
                             O_RDONLY, -1, 0, fd_conn, ack_num);
  } else {
    std::string filename = fbbcomm_serialized_dlopen_has_absolute_filename(msg) ?
                           fbbcomm_serialized_dlopen_get_absolute_filename(msg) : "NULL";
    proc->exec_point()->disable_shortcutting_bubble_up("Process failed to dlopen() ", filename);
    return 0;
  }
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_close *msg) {
  const int error = fbbcomm_serialized_close_get_error_no_with_fallback(msg, 0);
  return proc->handle_close(fbbcomm_serialized_close_get_fd(msg), error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_unlink *msg) {
  const int dirfd = fbbcomm_serialized_unlink_get_dirfd_with_fallback(msg, AT_FDCWD);
  const int flags = fbbcomm_serialized_unlink_get_flags_with_fallback(msg, 0);
  const int error = fbbcomm_serialized_unlink_get_error_no_with_fallback(msg, 0);
  return proc->handle_unlink(dirfd, fbbcomm_serialized_unlink_get_pathname(msg),
                             fbbcomm_serialized_unlink_get_pathname_len(msg), flags, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_rmdir *msg) {
  const int error = fbbcomm_serialized_rmdir_get_error_no_with_fallback(msg, 0);
  return proc->handle_rmdir(fbbcomm_serialized_rmdir_get_pathname(msg),
                            fbbcomm_serialized_rmdir_get_pathname_len(msg), error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_mkdir *msg) {
  const int dirfd = fbbcomm_serialized_mkdir_get_dirfd_with_fallback(msg, AT_FDCWD);
  const int error = fbbcomm_serialized_mkdir_get_error_no_with_fallback(msg, 0);
  return proc->handle_mkdir(dirfd, fbbcomm_serialized_mkdir_get_pathname(msg),
                            fbbcomm_serialized_mkdir_get_pathname_len(msg), error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fstat *msg) {
  const int st_mode = fbbcomm_serialized_fstat_get_st_mode_with_fallback(msg, 0);
  const int error = fbbcomm_serialized_fstat_get_error_no_with_fallback(msg, 0);
  const int fd = fbbcomm_serialized_fstat_get_fd_with_fallback(msg, -1);
  return proc->handle_fstat(fd, st_mode, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_stat *msg) {
  const int dirfd = fbbcomm_serialized_stat_get_dirfd_with_fallback(msg, AT_FDCWD);
  const int st_mode = fbbcomm_serialized_stat_get_st_mode_with_fallback(msg, 0);
  const int flags = fbbcomm_serialized_stat_get_flags_with_fallback(msg, 0);
  const int error = fbbcomm_serialized_stat_get_error_no_with_fallback(msg, 0);
  return proc->handle_stat(dirfd, fbbcomm_serialized_stat_get_filename(msg),
                           fbbcomm_serialized_stat_get_filename_len(msg),
                           flags, st_mode, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dup3 *msg) {
  const int error = fbbcomm_serialized_dup3_get_error_no_with_fallback(msg, 0);
  const int flags = fbbcomm_serialized_dup3_get_flags_with_fallback(msg, 0);
  return proc->handle_dup3(fbbcomm_serialized_dup3_get_oldfd(msg),
                           fbbcomm_serialized_dup3_get_newfd(msg),
                           flags, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_dup *msg) {
  const int error = fbbcomm_serialized_dup_get_error_no_with_fallback(msg, 0);
  return proc->handle_dup3(fbbcomm_serialized_dup_get_oldfd(msg),
                           fbbcomm_serialized_dup_get_ret(msg),
                           0, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_rename *msg) {
  const int olddirfd = fbbcomm_serialized_rename_get_olddirfd_with_fallback(msg, AT_FDCWD);
  const int newdirfd = fbbcomm_serialized_rename_get_newdirfd_with_fallback(msg, AT_FDCWD);
  const int error = fbbcomm_serialized_rename_get_error_no_with_fallback(msg, 0);
  return proc->handle_rename(olddirfd, fbbcomm_serialized_rename_get_oldpath(msg),
                             fbbcomm_serialized_rename_get_oldpath_len(msg),
                             newdirfd, fbbcomm_serialized_rename_get_newpath(msg),
                             fbbcomm_serialized_rename_get_newpath_len(msg), error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_symlink *msg) {
  const int newdirfd = fbbcomm_serialized_symlink_get_newdirfd_with_fallback(msg, AT_FDCWD);
  const int error = fbbcomm_serialized_symlink_get_error_no_with_fallback(msg, 0);
  return proc->handle_symlink(fbbcomm_serialized_symlink_get_oldpath(msg), newdirfd,
                              fbbcomm_serialized_symlink_get_newpath(msg), error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fcntl *msg) {
  const int error = fbbcomm_serialized_fcntl_get_error_no_with_fallback(msg, 0);
  int arg = fbbcomm_serialized_fcntl_get_arg_with_fallback(msg, 0);
  int ret = fbbcomm_serialized_fcntl_get_ret_with_fallback(msg, -1);
  return proc->handle_fcntl(fbbcomm_serialized_fcntl_get_fd(msg),
                            fbbcomm_serialized_fcntl_get_cmd(msg),
                            arg, ret, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_ioctl *msg) {
  const int error = fbbcomm_serialized_ioctl_get_error_no_with_fallback(msg, 0);
  int ret = fbbcomm_serialized_ioctl_get_ret_with_fallback(msg, -1);
  return proc->handle_ioctl(fbbcomm_serialized_ioctl_get_fd(msg),
                            fbbcomm_serialized_ioctl_get_cmd(msg),
                            ret, error);
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_read_from_inherited *msg) {
  const int error = fbbcomm_serialized_read_from_inherited_get_error_no_with_fallback(msg, 0);
  if (error == 0) {
    proc->handle_read_from_inherited(fbbcomm_serialized_read_from_inherited_get_fd(msg));
  }
  return 0;
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_write_to_inherited *msg) {
  const int error = fbbcomm_serialized_write_to_inherited_get_error_no_with_fallback(msg, 0);
  if (error == 0) {
    proc->handle_write_to_inherited(fbbcomm_serialized_write_to_inherited_get_fd(msg));
  }
  return 0;
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_chdir *msg) {
  const int error = fbbcomm_serialized_chdir_get_error_no_with_fallback(msg, 0);
  if (error == 0) {
    proc->handle_set_wd(fbbcomm_serialized_chdir_get_dir(msg),
                     fbbcomm_serialized_chdir_get_dir_len(msg));
  } else {
    proc->handle_fail_wd(fbbcomm_serialized_chdir_get_dir(msg));
  }
  return 0;
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_fchdir *msg) {
  const int error = fbbcomm_serialized_fchdir_get_error_no_with_fallback(msg, 0);
  if (error == 0) {
    proc->handle_set_fwd(fbbcomm_serialized_fchdir_get_fd(msg));
  }
  return 0;
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_pipe_request *msg,
                             int fd_conn) {
  const int flags = fbbcomm_serialized_pipe_request_get_flags_with_fallback(msg, 0);
  proc->handle_pipe_request(flags, fd_conn);
  return 0;
}

int ProcessPBAdaptor::handle(Process *proc, const FBBCOMM_Serialized_pipe_fds *msg) {
  const int fd0 = fbbcomm_serialized_pipe_fds_get_fd0(msg);
  const int fd1 = fbbcomm_serialized_pipe_fds_get_fd1(msg);
  proc->handle_pipe_fds(fd0, fd1);
  return 0;
}

}  /* namespace firebuild */
