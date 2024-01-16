/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef FIREBUILD_PROCESS_FBB_ADAPTOR_H_
#define FIREBUILD_PROCESS_FBB_ADAPTOR_H_

#include <string>
#include <vector>

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
  static int handle(Process *proc, const FBBCOMM_Serialized_pre_open *msg) {
    return proc->handle_pre_open(msg->get_dirfd_with_fallback(AT_FDCWD),
                                 msg->get_pathname(), msg->get_pathname_len());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_open *msg, int fd_conn, int ack_num) {
    const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
    int error = msg->get_error_no_with_fallback(0);
    int ret = msg->get_ret_with_fallback(-1);
    return proc->handle_open(dirfd, msg->get_pathname(), msg->get_pathname_len(), msg->get_flags(),
                             msg->get_mode_with_fallback(0), ret, error, fd_conn, ack_num,
                             msg->get_pre_open_sent(), msg->get_tmp_file_with_fallback(false));
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_freopen *msg, int fd_conn,
                    int ack_num) {
    int error = msg->get_error_no_with_fallback(0);
    int oldfd = msg->get_ret_with_fallback(-1);
    int ret = msg->get_ret_with_fallback(-1);
    return proc->handle_freopen(msg->get_pathname(), msg->get_pathname_len(), msg->get_flags(),
                                oldfd, ret, error, fd_conn, ack_num, msg->get_pre_open_sent());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_dlopen *msg, int fd_conn, int ack_num) {
    return proc->handle_dlopen(
        msg->get_libs_as_vector(),
        msg->has_filename() ? msg->get_filename() : nullptr,
        msg->has_filename() ? msg->get_filename_len() : 0,
        msg->get_error(), fd_conn, ack_num);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_close *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_close(msg->get_fd(), error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_closefrom *msg) {
    return proc->handle_closefrom(msg->get_lowfd());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_close_range *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_close_range(msg->get_first(), msg->get_last(), msg->get_flags(), error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_scandirat *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_scandirat(msg->get_dirfd_with_fallback(AT_FDCWD),
                                  msg->has_dirp() ? msg->get_dirp() : nullptr,
                                  msg->has_dirp() ? msg->get_dirp_len() : 0,
                                  error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_truncate *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_truncate(msg->get_pathname(), msg->get_pathname_len(),
                                 msg->get_length(), error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_unlink *msg) {
    const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
    const int flags = msg->get_flags_with_fallback(0);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_unlink(dirfd, msg->get_pathname(), msg->get_pathname_len(), flags, error,
                               msg->get_pre_open_sent());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_rmdir *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_rmdir(msg->get_pathname(), msg->get_pathname_len(), error,
                              msg->get_pre_open_sent());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_mkdir *msg) {
    const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_mkdir(dirfd, msg->get_pathname(), msg->get_pathname_len(), error,
                              msg->get_tmp_dir_with_fallback(false));
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_fstatat *msg) {
    const int fd = msg->get_fd_with_fallback(AT_FDCWD);
    const mode_t st_mode = msg->get_st_mode_with_fallback(0);
    const off_t st_size = msg->get_st_size_with_fallback(0);
    const int flags = msg->get_flags_with_fallback(0);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_fstatat(fd, msg->get_pathname(), msg->get_pathname_len(),
                                flags, st_mode, st_size, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_faccessat *msg) {
    const int dirfd = msg->get_dirfd_with_fallback(AT_FDCWD);
    const int mode = msg->get_mode();
    const int flags = msg->get_flags_with_fallback(0);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_faccessat(dirfd, msg->get_pathname(), msg->get_pathname_len(),
                                  mode, flags, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_fchmodat *msg) {
    const int fd = msg->get_fd_with_fallback(AT_FDCWD);
    const mode_t mode = msg->get_mode();
    const int flags = msg->get_flags_with_fallback(0);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_fchmodat(fd, msg->get_pathname(), msg->get_pathname_len(),
                                 mode, flags, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_shm_open *msg) {
    int error = msg->get_error_no_with_fallback(0);
    int ret = msg->get_ret_with_fallback(-1);
    return proc->handle_shm_open(msg->get_name(), msg->get_oflag(),
                                 msg->get_mode_with_fallback(0), ret, error);
  }

#ifdef __APPLE__
  static int handle(Process *proc, const FBBCOMM_Serialized_kqueue *msg) {
    return proc->handle_kqueue(msg->get_ret_with_fallback(-1),
                               msg->get_error_no_with_fallback(0));
  }
#endif

#ifdef __linux__
  static int handle(Process *proc, const FBBCOMM_Serialized_memfd_create *msg) {
    return proc->handle_memfd_create(msg->get_flags(), msg->get_ret());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_timerfd_create *msg) {
    return proc->handle_timerfd_create(msg->get_flags(), msg->get_ret());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_epoll_create *msg) {
    return proc->handle_epoll_create(msg->get_flags_with_fallback(0), msg->get_ret());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_eventfd *msg) {
    return proc->handle_eventfd(msg->get_flags(), msg->get_ret());
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_signalfd *msg) {
    return proc->handle_signalfd(msg->get_fd(), msg->get_flags(), msg->get_ret());
  }
#endif

  static int handle(Process *proc, const FBBCOMM_Serialized_dup *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_dup3(msg->get_oldfd(), msg->get_ret(), 0, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_dup3 *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    const int flags = msg->get_flags_with_fallback(0);
    return proc->handle_dup3(msg->get_oldfd(), msg->get_newfd(), flags, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_rename *msg) {
    const int olddirfd = msg->get_olddirfd_with_fallback(AT_FDCWD);
    const int newdirfd = msg->get_newdirfd_with_fallback(AT_FDCWD);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_rename(olddirfd, msg->get_oldpath(), msg->get_oldpath_len(),
                               newdirfd, msg->get_newpath(), msg->get_newpath_len(), error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_symlink *msg) {
    const int newdirfd = msg->get_newdirfd_with_fallback(AT_FDCWD);
    const int error = msg->get_error_no_with_fallback(0);
    return proc->handle_symlink(msg->get_target(), newdirfd, msg->get_newpath(), error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_fcntl *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    int arg = msg->get_arg_with_fallback(0);
    int ret = msg->get_ret_with_fallback(-1);
    return proc->handle_fcntl(msg->get_fd(), msg->get_cmd(), arg, ret, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_ioctl *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    int ret = msg->get_ret_with_fallback(-1);
    return proc->handle_ioctl(msg->get_fd(), msg->get_cmd(), ret, error);
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_read_from_inherited *msg) {
    proc->handle_read_from_inherited(msg->get_fd(), msg->get_is_pread());
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_write_to_inherited *msg) {
    proc->handle_write_to_inherited(msg->get_fd(), msg->get_is_pwrite());
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_seek_in_inherited *msg) {
    proc->handle_seek_in_inherited(msg->get_fd(), msg->get_modify_offset());
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_inherited_fd_offset *msg) {
    proc->handle_inherited_fd_offset(msg->get_fd(), msg->get_offset());
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_recvmsg_scm_rights *msg) {
    bool cloexec = msg->get_cloexec();
    std::vector<int> fds = msg->get_fds_as_vector();
    proc->handle_recvmsg_scm_rights(cloexec, fds);
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_umask *msg) {
    const mode_t old_umask = msg->get_ret();
    const mode_t new_umask = msg->get_mask();
    proc->handle_umask(old_umask, new_umask);
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_chdir *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    if (error == 0) {
      proc->handle_set_wd(msg->get_pathname(), msg->get_pathname_len());
    } else {
      proc->handle_fail_wd(msg->get_pathname());
    }
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_fchdir *msg) {
    const int error = msg->get_error_no_with_fallback(0);
    if (error == 0) {
      proc->handle_set_fwd(msg->get_fd());
    }
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_pipe_request *msg, int fd_conn) {
    const int flags = msg->get_flags_with_fallback(0);
    proc->handle_pipe_request(flags, fd_conn);
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_pipe_fds *msg) {
    const int fd0 = msg->get_fd0();
    const int fd1 = msg->get_fd1();
    proc->handle_pipe_fds(fd0, fd1);
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_socket *msg) {
    proc->handle_socket(msg->get_domain(), msg->get_type(), msg->get_protocol(),
                        msg->get_ret_with_fallback(-1), msg->get_error_no_with_fallback(0));
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_socketpair *msg) {
    proc->handle_socketpair(msg->get_domain(), msg->get_type(), msg->get_protocol(),
                            msg->get_fd0_with_fallback(-1), msg->get_fd1_with_fallback(-1),
                            msg->get_error_no_with_fallback(0));
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_connect *msg) {
    proc->handle_connect(msg->get_sockfd(), msg->get_error_no_with_fallback(0));
    return 0;
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_statfs *msg) {
    if (msg->has_pathname()) {
      return proc->handle_statfs(msg->get_pathname(), msg->get_pathname_len(),
                                 msg->get_error_no_with_fallback(0));
    } else {
      return proc->handle_statfs(nullptr, 0, msg->get_error_no_with_fallback(0));
    }
  }

  static int handle(Process *proc, const FBBCOMM_Serialized_mktemp *msg) {
    return proc->handle_mktemp(msg->get_template(), msg->get_template_len());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessFBBAdaptor);
};

#define PFBBA_HANDLE(process, tag, buffer)                              \
  ProcessFBBAdaptor::handle(                                            \
      process,                                                          \
      reinterpret_cast<const FBBCOMM_Serialized_##tag *>(buffer))

#define PFBBA_HANDLE_ACKED(process, tag, buffer, fd_conn, ack_num)      \
  ProcessFBBAdaptor::handle(                                            \
      process,                                                          \
      reinterpret_cast<const FBBCOMM_Serialized_##tag *>(buffer),       \
      fd_conn, ack_num)

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_FBB_ADAPTOR_H_
