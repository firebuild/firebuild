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


#include "firebuild/process.h"

#include <fcntl.h>
#include <signal.h>

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif
#include <sys/ioctl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/config.h"
#include "firebuild/execed_process.h"
#include "firebuild/hash_cache.h"
#include "firebuild/forked_process.h"
#include "firebuild/execed_process_env.h"
#include "firebuild/process_tree.h"
#include "firebuild/debug.h"
#include "firebuild/utils.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const int exec_count, const FileName *wd,
                 const mode_t umask, Process * parent, std::vector<std::shared_ptr<FileFD>>* fds,
                 const bool debug_suppressed)
    : parent_(parent),
      fb_pid_(fb_pid_counter++), pid_(pid), ppid_(ppid), exec_count_(exec_count), wd_(wd),
      umask_(umask), fds_(fds), fork_children_(), expected_child_(),
      debug_suppressed_(debug_suppressed) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "pid=%d, ppid=%d, parent=%s", pid, ppid, D(parent));
}

Process* Process::fork_parent() {
  return fork_point() ? fork_point()->parent() : nullptr;
}

const Process* Process::fork_parent() const {
  return fork_point() ? fork_point()->parent() : nullptr;
}

Process* Process::last_exec_descendant() {
  Process* ret = this;
  while (ret->exec_child()) {
    ret = ret->exec_child();
  }
  return ret;
}

const Process* Process::last_exec_descendant() const {
  const Process* ret = this;
  while (ret->exec_child()) {
    ret = ret->exec_child();
  }
  return ret;
}

void Process::update_rusage(const int64_t utime_u, const int64_t stime_u) {
  ExecedProcess* ep = exec_point();
  if (ep) {
    ep->add_utime_u(utime_u);
    ep->add_stime_u(stime_u);
  }
}

void Process::resource_usage(const int64_t utime_u, const int64_t stime_u) {
  update_rusage(utime_u, stime_u);
}

/* This is a static function operating on any std::vector<std::shared_ptr<FileFD>>,
 * without requiring a Process object. */
std::shared_ptr<FileFD>
Process::add_filefd(std::vector<std::shared_ptr<FileFD>>* fds,
                    const int fd,
                    std::shared_ptr<FileFD> ffd) {
  TRACK(FB_DEBUG_PROC, "fd=%d", fd);

  if (fds->size() <= static_cast<unsigned int>(fd)) {
    fds->resize(fd + 1, nullptr);
  }
  if ((*fds)[fd]) {
    firebuild::fb_error("Fd " + d(fd) + " is already tracked as being open.");
  }
  /* the shared_ptr takes care of cleaning up the old fd if needed */
  (*fds)[fd] = ffd;
  return ffd;
}

/* This is the member function operating on `this` Process. */
std::shared_ptr<FileFD>
Process::add_filefd(const int fd, std::shared_ptr<FileFD> ffd) {
  TRACK(FB_DEBUG_PROC, "fd=%d", fd);

  return Process::add_filefd(fds_, fd, ffd);
}

void Process::add_pipe(std::shared_ptr<Pipe> pipe) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "pipe=%s", D(pipe.get()));

  exec_point()->add_pipe(pipe);
}

void Process::drain_all_pipes() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  for (auto file_fd : *fds_) {
    if (!file_fd || !is_wronly(file_fd->flags())) {
      continue;
    }
    auto pipe = file_fd->pipe().get();
    if (pipe) {
      pipe->drain_fd1_end(file_fd.get());
    }
  }
}

std::vector<std::shared_ptr<FileFD>>* Process::pass_on_fds(const bool execed) const {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "execed=%s", D(execed));

  const int fds_size = fds_->size();
  auto ret_fds = new std::vector<std::shared_ptr<FileFD>>(fds_size);
  int last_fd = -1;
  for (int i = 0; i < fds_size; i++) {
    const FileFD* const raw_file_fd = (*fds_)[i].get();
    if (raw_file_fd != nullptr) {
      if (!(execed && raw_file_fd->cloexec())
#ifdef __APPLE__
          && !(!execed && raw_file_fd->close_on_fork())
#endif
          ) {
        /* The operations on the fds in the new process don't affect the fds in the parent,
         * thus create a copy of the parent's FileFD pointed to by a new shared pointer. */
        (*ret_fds)[i] = std::make_shared<FileFD>(*raw_file_fd);
        last_fd = i;
        if (execed && raw_file_fd->close_on_popen()) {
          /* The newly exec()-ed process will not close inherited popen()-ed fds on pclose() */
          (*ret_fds)[i]->set_close_on_popen(false);
        }
      }
    }
  }

  if (last_fd + 1 < fds_size) {
    /* A few of the last elements of the ret_fds vector is not used. Cut the size to the used
     * ones. */
    ret_fds->resize(last_fd + 1);
  }

  return ret_fds;
}

void Process::AddPopenedProcess(int fd, ExecedProcess *proc) {
  fd2popen_child_[fd] = proc;
}

int Process::handle_pre_open(const int dirfd, const char * const ar_name, const size_t ar_len) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "dirfd=%d, ar_name=%s", dirfd, D(ar_name));

  const FileName* name = get_absolute(dirfd, ar_name, ar_len);
  if (!name) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not find file name to mark as opened for writing");
    return -1;
  } else {
    name->open_for_writing(this->exec_point());
    return 0;
  }
}

int Process::handle_open(const int dirfd, const char * const ar_name, const size_t ar_len,
                         const int flags, const mode_t mode, const int fd, const int error,
                         int fd_conn, const int ack_num, const bool pre_open_sent,
                         const bool tmp_file) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "dirfd=%d, ar_name=%s, flags=%d, mode=0%03o, pre_open_sent=%d, fd=%d, error=%d, "
         "fd_conn=%s, ack_num=%d",
         dirfd, D(ar_name), flags, mode, fd, pre_open_sent, error, D_FD(fd_conn), ack_num);

  /* O_TMPFILE is actually multiple bits, 0x410000 */
#ifdef O_TMPFILE
  const bool o_tmpfile_set = (flags & O_TMPFILE) == O_TMPFILE;
#else
  const bool o_tmpfile_set = false;
#endif
  const FileName* name = get_absolute(dirfd, ar_name, ar_len);
  if (!name) {
    // FIXME don't disable shortcutting if openat() failed due to the invalid dirfd
    exec_point()->disable_shortcutting_bubble_up("Invalid dirfd passed to openat()");
    if (ack_num != 0) {
      ack_msg(fd_conn, ack_num);
    }
    return -1;
  }

  if (fd >= 0) {
    if (o_tmpfile_set) {
      /* open(..., O_TMPFILE) does not really open a file, but creates an unnamed temporary file in
       * the specified directory. Treat it like it was a memory backed file and later register the
       * directory's usage. */
      add_filefd(fd, std::make_shared<FileFD>(flags, FD_SPECIAL, this));
    } else {
      add_filefd(fd, std::make_shared<FileFD>(name, flags, this));
    }
  }

  if (pre_open_sent) {
    /* When pre_open is sent the not interceptor nor the supervisor knew the outcome of open, but
     * the path's refcount is increased in the supervisor (+1).
     * In handle_open() std::make_shared<FileFD>(name, fd, flags, this) increases the refcount again
     * due to the FileFD construction if the file really got opened (+2), or the refcount is not
     * touched in case of an error (still +1). In both cases the refcount has to be decremented to
     * reflect actual usage (+1 vs +0) after the open(). */
    name->close_for_writing();
  }

  /* If O_TMPFILE was set we register the parent directory and it is not treated as a temporary
   * dir here. */
  FileUsageUpdate update =
      FileUsageUpdate::get_from_open_params(name, o_tmpfile_set ? O_RDWR | O_DIRECTORY : flags,
                                            mode & 07777 & ~umask(), error,
                                            o_tmpfile_set ? false : tmp_file);
  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up("Could not register the opening of a file", *name);
    if (ack_num != 0) {
      ack_msg(fd_conn, ack_num);
    }
    return -1;
  }

  /* handle_open() is designed to possibly send an early ACK. However it doesn't do so currently,
   * until we figure out #878 / #879. It always sends the ACK just before returning. */
  if (ack_num != 0) {
    ack_msg(fd_conn, ack_num);
  }
  return 0;
}

/* Handle freopen(). See #650 for some juicy details. */
int Process::handle_freopen(const char * const ar_name, const size_t ar_len,
                            const int flags, const int oldfd, const int fd,
                            const int error, int fd_conn, const int ack_num,
                            const bool pre_open_sent) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "ar_name=%s, flags=%d, oldfd=%d, fd=%d, error=%d, fd_conn=%s, ack_num=%d",
         D(ar_name), flags, oldfd, fd, error, D_FD(fd_conn), ack_num);

  if (ar_name != NULL) {
    /* old_fd is always closed, even if freopen() fails. It comes from stdio's bookkeeping, we have
     * no reason to assume that this close attempt failed. */
    handle_close(oldfd, 0);

    /* Register the opening of the new file, no matter if succeeded or failed. */
    return handle_open(AT_FDCWD, ar_name, ar_len, flags, 0666, fd, error, fd_conn,
                       ack_num, pre_open_sent, false);
  } else {
    /* Without a name pre_open should not have been sent. */
    assert(!pre_open_sent);
    /* Find oldfd. */
    FileFD *file_fd = get_fd(oldfd);
    if (!file_fd || file_fd->filename() == nullptr) {
      /* Can't find oldfd, or wasn't opened by filename. Don't know what to do. */
      exec_point()->disable_shortcutting_bubble_up(
        "Could not figure out old file name for freopen(..., NULL)", oldfd);
      if (ack_num != 0) {
        ack_msg(fd_conn, ack_num);
      }
      return -1;
    } else {
      /* Remember oldfd's filename before closing oldfd. We've tried to reopen the same file. */
      const FileName *filename = file_fd->filename();

      /* old_fd is always closed, even if freopen() fails. It comes from stdio's bookkeeping, we have
       * no reason to assume that this close attempt failed. */
      handle_close(oldfd, 0);

      /* Register the reopening, no matter if succeeded or failed. */
      return handle_open(AT_FDCWD, filename->c_str(), filename->length(),
                         flags, 0666, fd, error, fd_conn, ack_num, pre_open_sent, false);
    }
  }
}

/* close that fd if open, silently ignore if not open */
int Process::handle_force_close(const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d", fd);

  if (get_fd(fd)) {
    return handle_close(fd, 0);
  }
  return 0;
}

void Process::handle_close(FileFD * file_fd, const int fd) {
  auto pipe = file_fd->pipe().get();
  if (pipe) {
    /* There may be data pending, drain it and register closure. */
    pipe->handle_close(file_fd);
    file_fd->set_pipe(nullptr);
  }
  (*fds_)[fd].reset();
}

int Process::handle_close(const int fd, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, error=%d", fd, error);

  if (error == EIO) {
    exec_point()->disable_shortcutting_bubble_up("IO error closing fd", fd);
    return -1;
  } else if (error == EINTR) {
    /* We don't know if the fd was closed or not, see #723. */
    exec_point()->disable_shortcutting_bubble_up("EINTR while closing fd", fd);
    return -1;
  } else if (error == EBADF) {
    /* Process closed an fd unknown to it. Who cares? */
    assert(!get_fd(fd));
    return 0;
  } else {
    FileFD* file_fd = get_fd(fd);
    if (!file_fd) {
      if (error == 0) {
        exec_point()->disable_shortcutting_bubble_up(
            "Process closed an unknown fd successfully, "
            "which means interception missed at least one open()", fd);
        return -1;
      } else {
        exec_point()->disable_shortcutting_bubble_up(
            "Process closed an unknown, but valid fd unsuccessfully, "
            "which could mean that interception missed at least one open()", fd);
        return -1;
      }
    } else {
      handle_close(file_fd, fd);
      return 0;
    }
  }
}

int Process::handle_closefrom(const int lowfd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "lowfd=%d", lowfd);

  return handle_close_range(lowfd, UINT_MAX, 0, 0);
}

int Process::handle_close_range(const unsigned int first, const unsigned int last,
                                const int flags, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "first=%u, last=%u, flags=%d, error=%d",
         first, last, flags, error);

  (void)flags;  /* might be unused */

  if (!error) {
    unsigned int fds_size = fds_->size();
    const int range_max = std::min(last, fds_size - 1);
    for (int fd = first; fd <= range_max; fd++) {
      FileFD* file_fd = (*fds_)[fd].get();
      if (!file_fd) {
        continue;
      }
      if (flags & CLOSE_RANGE_CLOEXEC) {
        /* Don't close, just set the cloexec bit. */
        file_fd->set_cloexec(true);
      } else {
        handle_close(fd);
      }
    }
  }
  return 0;
}


int Process::handle_dlopen(const std::vector<std::string>& libs,
                           const char * const looked_up_filename,
                           const size_t looked_up_filename_len,
                           const bool error, int fd_conn, int ack_num) {
  int ret = 0;
  if (libs.size() > 0) {
    /* There are new loaded shared libraries, register them. */
    for (const std::string& ar_name : libs) {
      const FileName* name = get_absolute(AT_FDCWD, ar_name.c_str(), ar_name.size());
#ifdef __APPLE__
      if (!hash_cache->get_statinfo(name, nullptr, nullptr)) {
        /* Some libraries are not present as regular files on macOS, ignore those. */
        continue;
      }
#endif
      FileUsageUpdate update = FileUsageUpdate::get_from_open_params(name, O_RDONLY, 0, 0, false);
      if (!exec_point()->register_file_usage_update(name, update)) {
        exec_point()->disable_shortcutting_bubble_up(
            "Could not register loading the shared library", *name);
      }
    }
  }
  if (error) {
    /* When failing to dlopen() a file assume it is not present.
     * This is a safe assumption for shortcutting purposes, since the cache entry
     * will require the the file to be missing to shortcut the process and if the file
     * is missing dlopen() would have failed for sure.
     */
    if (strchr(looked_up_filename, '/')) {
      /* Failed to dlopen() a relative or absolute filename, register the file as missing. */
      handle_open(AT_FDCWD, looked_up_filename, looked_up_filename_len,
                  O_RDONLY, 0, -1, ENOENT, 0, 0, false, false);
    } else {
      /* Failed dlopen() could not find the file on the search path. */
      /* TODO(rbalint) allow shortcutting the process and mark the file as missing on all the
       * search path entries as described in dlopen(3). */
      exec_point()->disable_shortcutting_bubble_up("Process failed to dlopen() ",
                                                   looked_up_filename);
      ret = -1;
    }
  }
  // TODO(rbalint) maybe move ACK-ing earlier, before the bubble-ups
  if (ack_num != 0) {
    ack_msg(fd_conn, ack_num);
  }
  return ret;
}

int Process::handle_scandirat(const int dirfd, const char * const ar_name, const size_t ar_len,
                              const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "dirfd=%d, ar_name=%s, error=%d", dirfd, D(ar_name), error);

  if (error) {
    if (error == ENOENT) {
      /* scandirat() failed, because the directory doesn't exist. */
      const FileName* name = get_absolute(dirfd, ar_name, ar_len);
      if (!name) {
        exec_point()->disable_shortcutting_bubble_up(
            "Invalid dirfd or filename passed to scandirat()");
        return -1;
      }
      FileUsageUpdate update(name, NOTEXIST);
      if (!exec_point()->register_file_usage_update(name, update)) {
        exec_point()->disable_shortcutting_bubble_up("Could not register scandirat() on", *name);
        return -1;
      }
      return 0;
    } else {
      /* scandirat() failed, but we don't know why. */
      exec_point()->disable_shortcutting_bubble_up("scandirat() failed");
      return -1;
    }
  }

  const FileName* name = get_absolute(dirfd, ar_name, ar_len);
  if (!name) {
    exec_point()->disable_shortcutting_bubble_up(
        "Invalid dirfd or filename passed to scandirat()");
    return -1;
  }

  FileUsageUpdate update =
       FileUsageUpdate::get_from_open_params(name, O_RDONLY | O_DIRECTORY, 0, error, false);
  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up("Could not register scandirat() on", *name);
    return -1;
  }

  return 0;
}

int Process::handle_truncate(const char * const ar_name, const size_t ar_len,
                             const off_t length, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "ar_name=%s, length=%" PRIoff ", error=%d", D(ar_name), length, error);

  const FileName* name = get_absolute(AT_FDCWD, ar_name, ar_len);
  if (!name) {
    exec_point()->disable_shortcutting_bubble_up("Could not find file to truncate()");
    return -1;
  }
  /* truncate() always sends pre_open. */
  name->close_for_writing();

  // FIXME Will be a bit tricky to implement shortcutting, see #637.
  (void)length;
  (void)error;
  exec_point()->disable_shortcutting_bubble_up("truncate() is not supported");
  return 0;
}

int Process::handle_unlink(const int dirfd, const char * const ar_name, const size_t ar_len,
                           const int flags, const int error, const bool pre_open_sent) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "dirfd=%d, ar_name=%s, flags=%d, error=%d, pre_open_sent=%d",
         dirfd, D(ar_name), flags, error, pre_open_sent);

  const FileName* name = get_absolute(dirfd, ar_name, ar_len);
  if (!name) {
    // FIXME don't disable shortcutting if unlinkat() failed due to the invalid dirfd
    exec_point()->disable_shortcutting_bubble_up("Invalid dirfd passed to unlinkat()");
    return -1;
  }
  if (pre_open_sent) {
    name->close_for_writing();
  }

  if (!error) {
    /* There is no need to call register_parent_directory().
     * If the process created the file to unlink it is already registered and if it was present
     * before the process started then registering the file to unlink already implies the existence
     * of the parent dir.
     */

    // FIXME When a directory is removed, register that it was an _empty_ directory
    FileUsageUpdate update = FileUsageUpdate(name, flags & AT_REMOVEDIR ? ISDIR : ISREG, true);
    if (!exec_point()->register_file_usage_update(name, update)) {
      exec_point()->disable_shortcutting_bubble_up(
          "Could not register the unlink or rmdir", *name);
      return -1;
    }
  } else if (error == ENOENT) {
    FileUsageUpdate update(name, NOTEXIST);
    if (!exec_point()->register_file_usage_update(name, update)) {
      exec_point()->disable_shortcutting_bubble_up("Could not register the unlink or rmdir", *name);
      return -1;
    }
  }

  return 0;
}

int Process::handle_fstatat(const int fd, const char * const ar_name, const size_t ar_len,
                            const int flags, const mode_t st_mode, const off_t st_size,
                            const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "fd=%d, ar_name=%s, flags=%d, st_mode=%d, st_size=%" PRIoff ", error=%d",
         fd, D(ar_name), flags, st_mode, st_size, error);

  const FileName *name;

#ifndef AT_EMPTY_PATH
  (void)flags;
#endif

  if (ar_name == nullptr
#ifdef AT_EMPTY_PATH
      || (ar_name[0] == '\0' && (flags & AT_EMPTY_PATH))
#endif
      ) {
    /* Operating on an opened fd, i.e. fstat() or fstatat("", AT_EMPTY_PATH). */
    FileFD *file_fd = get_fd(fd);
    if (!file_fd) {
      if (error == 0) {
        exec_point()->disable_shortcutting_bubble_up(
            "Process fstatat()ed an unknown fd successfully, "
            "which means interception missed at least one open()", fd);
        return -1;
      } else {
        /* Invalid fd passed to fstat(), or something like that. */
        return 0;
      }
    }

    name = file_fd->filename();
    if (!name) {
      /* Cannot find file name, maybe it's a pipe or similar. Take no action. */
      return 0;
    }
  } else {
    /* Operating on a file reached by its name, like [l]stat(), or fstatat() with a non-empty
     * path relative to some dirfd (called 'fd' here). */
    name = get_absolute(fd, ar_name, ar_len);
    if (!name) {
      // FIXME don't disable shortcutting if stat() failed due to the invalid dirfd
      exec_point()->disable_shortcutting_bubble_up(
          "Invalid dirfd or filename passed to stat() variant");
      return -1;
    }
  }

  FileUsageUpdate update = FileUsageUpdate::get_from_stat_params(name, st_mode, st_size, error);
  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up("Could not register fstatat() on", *name);
    return -1;
  }

  return 0;
}

int Process::handle_statfs(const char * const a_name, const size_t length,
                           const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "a_name=%s, error=%d", D(a_name), error);

  if (a_name == nullptr) {
    /* Operating on an opened fd, i.e. fstatfs(). */
    if (quirks & FB_QUIRK_IGNORE_STATFS) {
      return 0;
    } else {
      exec_point()->disable_shortcutting_bubble_up(
          "fstatfs() family operating on fds is not supported");
      return -1;
    }
  }
  /* Operating on a file reached by its name, made absolute by the interceptor. */
#ifdef FB_EXTRA_DEBUG
  assert(path_is_absolute(a_name));
#endif
  const FileName* name = FileName::Get(a_name, length);
  if (error == ENOENT) {
    FileUsageUpdate update(name, NOTEXIST);
    if (!exec_point()->register_file_usage_update(name, update)) {
      exec_point()->disable_shortcutting_bubble_up("Could not register failed statfs()", *name);
    }
  } else if (!(quirks & FB_QUIRK_IGNORE_STATFS)) {
    // TODO(rbalint) add more supported cases
    exec_point()->disable_shortcutting_bubble_up(
        "Successful statfs() calls are not supported.");
    return -1;
  }
  return 0;
}

int Process::handle_mktemp(const char * const a_name, const size_t length) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "a_name=%s", D(a_name));

  /* Operating on a file reached by its name, made absolute by the interceptor. */
#ifdef FB_EXTRA_DEBUG
  assert(path_is_absolute(a_name));
#endif
  const FileName* name = FileName::Get(a_name, length);
  /* The only thing the process knows about that file is that it does not exist. */
  FileUsageUpdate update = FileUsageUpdate::get_from_open_params(name, O_RDWR, 0, ENOENT, true);
  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up("Could not register mktemp()", *name);
  }
  return 0;
}

int Process::handle_faccessat(const int dirfd, const char * const ar_name, const size_t ar_name_len,
                              const int mode, const int flags, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "dirfd=%d, ar_name=%s, mode=%d, flags=%d, error=%d",
         dirfd, D(ar_name), mode, flags, error);

  (void)flags;  /* AT_EACCESS is currently ignored, we assume no setuid/setgid in the game. */

  /* Note: faccessat() obviously cannot operate on an already opened file, doesn't support
   * AT_EMPTY_PATH. */
  const FileName* name = get_absolute(dirfd, ar_name, ar_name_len);
  if (!name) {
    // FIXME don't disable shortcutting if chmod() failed due to the invalid dirfd
    exec_point()->disable_shortcutting_bubble_up(
        "Invalid dirfd or filename passed faccessat()");
    return -1;
  }

  FileUsageUpdate update(name);

  if (mode == F_OK) {
    /* Checked if there's something at this name. It's effectively a stat(), without getting to know
     * the fields in "struct stat". */
    if (!error) {
      update.set_initial_type(EXIST);
    } else {
      update.set_initial_type(NOTEXIST);
    }
  } else {
    /* We got to know something about some of the read, write, execute permission bits. We assume it
     * corresponds to the owner's permission bit. By this we assume that the current user isn't
     * root, that there isn't a setuid/setgid bit in the game, and that there isn't any read-only
     * filesystem involved. */
    if (!error) {
      /* Something exists at the given location, and all of the requested bits are set. */
      update.set_initial_type(EXIST);
      if (mode & R_OK) {
        update.set_initial_mode_bits(S_IRUSR, S_IRUSR);  /* 0400 */
      }
      if (mode & W_OK) {
        update.set_initial_mode_bits(S_IWUSR, S_IWUSR);  /* 0200 */
      }
      if (mode & X_OK) {
        update.set_initial_mode_bits(S_IXUSR, S_IXUSR);  /* 0100 */
      }
    } else if (error == EACCES) {
      /* The requested permissions aren't set. Unfortunately we don't know if the problem is with
       * this particular or some preceding path component, perhaps the access()ed file doesn't even
       * exist. Also, if multiple bits were requested then we don't know which of them are unset or
       * otherwise problematic, and which ones are set.
       * stat() the file, and remember the relevant bits from that information. */
      struct stat64 st;
      if (stat64(name->c_str(), &st) < 0) {
        /* There's a permission error somewhere earlier along the PATH. Firebuild does not support
         * this error anywhere else, we always pretend the file doesn't exist. Do that here, too.
         * FIXME This will be fixed across Firebuild by #862. */
        update.set_initial_type(NOTEXIST);
      } else {
        /* We know all the permission bits of the file. Record the ones that were requested by the
         * access() call. */
        update.set_initial_type(EXIST);
        bool unset_bit_seen = false;
        if (mode & R_OK) {
          update.set_initial_mode_bits(st.st_mode & S_IRUSR, S_IRUSR);  /* 0400 */
          unset_bit_seen = unset_bit_seen || ((st.st_mode & S_IRUSR) == 0);
        }
        if (mode & W_OK) {
          update.set_initial_mode_bits(st.st_mode & S_IWUSR, S_IWUSR);  /* 0200 */
          unset_bit_seen = unset_bit_seen || ((st.st_mode & S_IWUSR) == 0);
        }
        if (mode & X_OK) {
          update.set_initial_mode_bits(st.st_mode & S_IXUSR, S_IXUSR);  /* 0100 */
          unset_bit_seen = unset_bit_seen || ((st.st_mode & S_IXUSR) == 0);
        }
        if (!unset_bit_seen) {
          /* We've stat()ed the file, and all the permission bits requested by access() are set.
           * Why did access() return EACCES then?? It's a mystery. */
          exec_point()->disable_shortcutting_bubble_up("stat() returned unexpected permission bits "
                                                       "for", *name);
          return -1;
        }
      }
    } else {
      /* The entry doesn't exist, or at least we cannot handle it. */
      update.set_initial_type(NOTEXIST);
    }
  }

  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not register the faccessat of a file", *name);
    return -1;
  }

  return 0;
}

int Process::handle_fchmodat(const int fd, const char * const ar_name, const size_t ar_len,
                             const mode_t mode, const int flags, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "fd=%d, ar_name=%s, mode=%d, flags=%d, error=%d",
         fd, D(ar_name), mode, flags, error);

  (void)mode;  /* No need to remember what we chmod to, we'll stat at the end. */

  const FileName *name;
  ExecedProcess *register_from = exec_point();

  /* Linux's fchmodat() doesn't support AT_EMPTY_PATH. The attempt to fix it at
   * https://patchwork.kernel.org/project/linux-fsdevel/patch/148830142269.7103.7429913851447595016.stgit@bahia/
   * has apparently stalled.
   *
   * FreeBSD supports it.
   *
   * Let's just go on assuming it's supported to make our code consistent with fstatat(), future
   * fchownat() and possibly some other methods too. */

#ifndef AT_EMPTY_PATH
  (void)flags;
#endif

  if (ar_name == nullptr
#ifdef AT_EMPTY_PATH
      || (ar_name[0] == '\0' && (flags & AT_EMPTY_PATH))
#endif
      ) {
    /* Operating on an opened fd, i.e. fchmod() or fchmodat("", AT_EMPTY_PATH). */
    FileFD *file_fd = get_fd(fd);
    if (!file_fd) {
      if (error == 0) {
        exec_point()->disable_shortcutting_bubble_up(
            "Process fchmodat()ed an unknown fd successfully, "
            "which means interception missed at least one open()", fd);
        return -1;
      } else {
        /* Invalid fd passed to fchmod(), or something like that. */
        return 0;
      }
    }

    /* Disable shortcutting the processes that inherited this fd. If the fd was opened by the
     * current process (or a fork ancestor), as it's usually the case, then this is an empty set.
     * See #927. */
    register_from = file_fd->opened_by() ? file_fd->opened_by()->exec_point() : nullptr;
    exec_point()->disable_shortcutting_bubble_up_to_excl(
        register_from,
        "fchmod() on an inherited fd not supported");

    name = file_fd->filename();
    if (!name) {
      /* Cannot find file name, maybe it's a pipe or similar. Take no further action. */
      return 0;
    }
  } else {
    /* Operating on a file reached by its name, like [l]chmod(), or fchmodat() with a non-empty
     * path relative to some dirfd (called 'fd' here). */
    name = get_absolute(fd, ar_name, ar_len);
    if (!name) {
      // FIXME don't disable shortcutting if chmod() failed due to the invalid dirfd
      exec_point()->disable_shortcutting_bubble_up(
          "Invalid dirfd or filename passed fchmodat()");
      return -1;
    }
  }

  if (error) {
    if (register_from) {
      register_from->disable_shortcutting_bubble_up("Cannot register a failed fchmodat() on",
                                                    *name);
    }
    return -1;
  }

  FileUsageUpdate update(name, EXIST, false, true);
  if (register_from) {
    if (!register_from->register_file_usage_update(name, update)) {
      register_from->disable_shortcutting_bubble_up("Could not register fchmodat() on", *name);
      return -1;
    }
  }

  return 0;
}

int Process::handle_shm_open(const char * const name, const int oflag,
                             const mode_t mode, const int fd, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s, oflag=%d, mode=%d, fd=%d, error=%d",
         name, oflag, mode, fd, error);
  (void)name;
  (void)oflag;
  (void)mode;

  if (fd == -1) {
    switch (error) {
      case ENAMETOOLONG:
        /* This does not affect ability to shortcut*/
        return 0;
      default:
        break;
    }
#ifdef __APPLE__
  } else {
    if (strcmp("com.apple.featureflags.shm", name) == 0
        || strcmp("apple.shm.notification_center", name) == 0
        || strncmp("apple.cfprefs", name, strlen("apple.cfprefs")) == 0) {
      /* Register opened fd and don't disable shortcutting. */
      // TODO(rbalint) check contents of the shared memory and possibly
      // include parts in the fingerprint
      add_filefd(fd, std::make_shared<FileFD>(oflag | O_CLOEXEC, FD_SPECIAL, this));
      return 0;
    }
#endif
  }
  exec_point()->disable_shortcutting_bubble_up("shm_open() is not supported");
  return 0;
}

#ifdef __APPLE__
int Process::handle_kqueue(const int fd, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, error=%d", fd, error);
  if (fd != -1) {
    add_filefd(fd, std::make_shared<FileFD>(0, FD_SPECIAL, this))->set_close_on_fork(true);
  }
  return 0;
}
#endif

#ifdef __linux__
int Process::handle_memfd_create(const int flags, const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "flags=%d, fd=%d", flags, fd);
  add_filefd(fd, std::make_shared<FileFD>((flags & MFD_CLOEXEC) ? O_CLOEXEC : 0,
                                          FD_SPECIAL, this));
  return 0;
}

int Process::handle_timerfd_create(const int flags, const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "flags=%d, fd=%d", flags, fd);
  add_filefd(fd, std::make_shared<FileFD>((flags & TFD_CLOEXEC) ? O_CLOEXEC : 0,
                                          FD_SPECIAL, this));
  return 0;
}

#ifdef __linux__
int Process::handle_epoll_create(const int flags, const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "flags=%d, fd=%d", flags, fd);
  add_filefd(fd, std::make_shared<FileFD>((flags & EPOLL_CLOEXEC) ? O_CLOEXEC : 0,
                                          FD_SPECIAL, this));
  return 0;
}
#endif

int Process::handle_eventfd(const int flags, const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "flags=%d, fd=%d", flags, fd);
  add_filefd(fd, std::make_shared<FileFD>((flags & EFD_CLOEXEC) ? O_CLOEXEC : 0,
                                          FD_SPECIAL, this));
  return 0;
}

int Process::handle_signalfd(const int oldfd, const int flags, const int newfd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "oldfd=%d, flags=%d newfd=%d", oldfd, flags, newfd);
  if (oldfd == -1) {
    add_filefd(newfd, std::make_shared<FileFD>((flags & SFD_CLOEXEC) ? O_CLOEXEC : 0,
                                               FD_SPECIAL, this));
  } else {
    /* Reusing old fd, nothing to do.*/
    // TODO(rbalint) maybe the O_CLOEXEC flag could be updated, but the man page sounds like as
    // if the flags are not used when reusing the old fd.
  }
  return 0;
}
#endif

int Process::handle_rmdir(const char * const ar_name, const size_t ar_name_len, const int error,
                          const bool pre_open_sent) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "ar_name=%s, error=%d, pre_open_sent=%d",
         D(ar_name), error, pre_open_sent);

  return handle_unlink(AT_FDCWD, ar_name, ar_name_len, AT_REMOVEDIR, error, pre_open_sent);
}

int Process::handle_mkdir(const int dirfd, const char * const ar_name, const size_t ar_len,
                          const int error, const bool tmp_dir) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "dirfd=%d, ar_name=%s, error=%d",
         dirfd, D(ar_name), error);

  const FileName* name = get_absolute(dirfd, ar_name, ar_len);
  if (!name) {
    // FIXME don't disable shortcutting if mkdirat() failed due to the invalid dirfd
    exec_point()->disable_shortcutting_bubble_up("Invalid dirfd passed to mkdirat()");
    return -1;
  }

  FileUsageUpdate update = FileUsageUpdate::get_from_mkdir_params(name, error, tmp_dir);
  if (!exec_point()->register_file_usage_update(name, update)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not register the directory creation ", *name);
    return -1;
  }

  return 0;
}

void Process::handle_pipe_request(const int flags, const int fd_conn) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "flags=%d", flags);

  pending_pipe_t pending_pipe {};

  /* Creating a pipe consists of multiple steps, but they are all guarded by a single lock in the
   * interceptor, so we can't have two pipe creations running in parallel in the same Process. */
  assert(!proc_tree->Proc2PendingPipe(this));

  /* Create an intercepted unnamed pipe, which is actually two unnamed pipes: one from the
   * interceptor up to the supervisor, and one from the supervisor back down to the interceptor. */
  int up[2], down[2];

  FBBCOMM_Builder_pipe_created response;

  if (fb_pipe2(up, flags) < 0) {
    response.set_error_no(errno);
    send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&response));
    return;
  }
  if (fb_pipe2(down, flags) < 0) {
    response.set_error_no(errno);
    send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&response));
    close(up[0]);
    close(up[1]);
    return;
  }
  if (epoll->is_added_fd(down[1])) {
    down[1] = epoll->remap_to_not_added_fd(down[1]);
  }

  /* Send the "pipe_created" message with two attached fds.
   * down[0] becomes pipefd[0], up[1] becomes pipefd[1] in the interceptor. */
  int fds_to_send[2] = { down[0], up[1] };
  send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&response), fds_to_send, 2);

  /* The endpoints we've just sent are no longer needed in the supervisor. */
  close(down[0]);
  close(up[1]);

  /* Remember the other two fds, we'll need them in the "pipe_fds" step.
   * According to the supervisor terminology:
   *  - fd0 corresponds to the original pipe's fd0, where we're writing to, which is now down[1];
   *  - fd1 corresponds to the original pipe's fd1, where we're reading from, which is now up[0]. */
  pending_pipe.fd0 = down[1];
  pending_pipe.fd1 = up[0];
  pending_pipe.flags = flags;

  /* We need the supervisor end of theses pipes to be nonblocking. Maybe they already are. */
  if (!(flags & O_NONBLOCK)) {
    fcntl(pending_pipe.fd0, F_SETFL, flags | O_NONBLOCK);
    fcntl(pending_pipe.fd1, F_SETFL, flags | O_NONBLOCK);
  }

  firebuild::bump_fd_age(pending_pipe.fd0);
  firebuild::bump_fd_age(pending_pipe.fd1);

  proc_tree->QueuePendingPipe(this, pending_pipe);

  /* To be continued in handle_pipe_fds() upon the next interceptor message. */
}

void Process::handle_pipe_fds(const int fd0, const int fd1) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd0=%d fd1=%d", fd0, fd1);

  /* Continuing from a previous handle_pipe_request(). */

  /* Creating a pipe consists of multiple steps, but they are all guarded by a single lock in the
   * interceptor, so we can't have two pipe creations running in parallel in the same Process. */
  const pending_pipe_t *pending_pipe = proc_tree->Proc2PendingPipe(this);
  assert(pending_pipe != nullptr);

  /* Validate fds. */
  if (get_fd(fd0)) {
    /* We already have this fd, probably missed a close(). */
    exec_point()->disable_shortcutting_bubble_up(
        "Process created an fd which is known to be open, "
        "which means interception missed at least one close()", fd0);
    close(pending_pipe->fd0);
    close(pending_pipe->fd1);
  } else if (get_fd(fd1)) {
    /* we already have this fd, probably missed a close(). */
    exec_point()->disable_shortcutting_bubble_up(
        "Process created an fd which is known to be open, "
        "which means interception missed at least one close()", fd1);
    close(pending_pipe->fd0);
    close(pending_pipe->fd1);
  } else {
#ifdef __clang_analyzer__
    /* Scan-build reports a false leak for the correct code. This is used only in static
     * analysis. It is broken because all shared pointers to the Pipe must be copies of
     * the shared self pointer stored in it. */
    auto pipe = std::make_shared<Pipe>(pending_pipe->fd0, this);
#else
    auto pipe = (new Pipe(pending_pipe->fd0, this))->shared_ptr();
#endif
    add_filefd(fd0, std::make_shared<FileFD>(
        (pending_pipe->flags & ~O_ACCMODE) | O_RDONLY, pipe->fd0_shared_ptr(), this, false));

    auto ffd1 = std::make_shared<FileFD>((pending_pipe->flags & ~O_ACCMODE) | O_WRONLY,
                                         pipe->fd1_shared_ptr(),
                                         this, false);
    add_filefd(fd1, ffd1);
    /* Empty recorders array. We don't start recording after a pipe(), this data wouldn't be
     * used anywhere. We only start recording after an exec(), to catch the traffic as seen
     * from that potential shortcutting point. */
    auto recorders = std::vector<std::shared_ptr<PipeRecorder>>();
    pipe->add_fd1_and_proc(pending_pipe->fd1, ffd1.get(), exec_point(), recorders);

    add_pipe(pipe);
  }

  /* Back to the default state. */
  proc_tree->DropPendingPipe(this);
}

void Process::handle_socket(const int domain, const int type, const int protocol, const int ret,
                            const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "domain=%d, type=%d, protocol=%d, ret=%d, error=%d",
         domain, type, protocol, ret, error);
  /* Creating a socket is fine from shortcutting POV as long as no communication takes place
   * over it. The created fd needs to be tracked, though. */
  (void)domain;
  (void)protocol;
  if (!error) {
    if (get_fd(ret)) {
      /* We already have this fd, probably missed a close(). */
      exec_point()->disable_shortcutting_bubble_up(
          "Process created an fd which is known to be open, "
          "which means interception missed at least one close()", ret);
      handle_close(ret);
    }
    const int flags = O_RDWR
#ifdef SOCK_CLOEXEC
        | ((type & SOCK_CLOEXEC) ? O_CLOEXEC : 0)
#endif
#ifdef SOCK_NONBLOCK
        | ((type & SOCK_NONBLOCK) ? O_NONBLOCK : 0)
#endif
      | 0;
    add_filefd(ret, std::make_shared<FileFD>(flags, FD_SPECIAL, this));
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    switch (type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK)) {
#elif defined(SOCK_CLOEXEC)
#error ""
#elif defined(SOCK_NONBLOCK)
#else
    switch (type) {
#endif
#if defined(SOCK_DGRAM)
      case SOCK_DGRAM:
#endif
#if defined(SOCK_RAW)
      case SOCK_RAW:
#endif
#if defined(SOCK_PACKET)
      case SOCK_PACKET:
#endif
#if defined(SOCK_DGRAM) || defined(SOCK_RAW) || defined(SOCK_PACKET)
        {
        exec_point()->disable_shortcutting_bubble_up(
            "SOCK_DGRAM, SOCK_RAW and SOCK_PACKET sockets are not supported");
        break;
      }
#endif
      default:
        /* Other socket types are OK since they require connect/bind/listen operations before
         * data can be exchanged with other processes and those disable shortcutting. */
        break;
    }
  } else {
    // TODO(rbalint) maybe add the result as a process input and allow shortcutting
    // in the same circumstances, for example when hitting EACCESS in restricted build
    // environment.
    exec_point()->disable_shortcutting_bubble_up("socket() call failed");
  }
}

void Process::handle_socketpair(const int domain, const int type, const int protocol,
                                const int fd0, const int fd1, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "domain=%d, type=%d, protocol=%d, fd0=%d, fd1=%d, error=%d",
         domain, type, protocol, fd0, fd1, error);
  /* Creating a socketpair is fine from shortcutting POV, it behaves like an anonymous file
   * (e.g. memfd). */
  (void)domain;
  (void)protocol;
#if !defined(SOCK_CLOEXEC) && !defined(SOCK_NONBLOCK)
  (void)type;
#endif
  if (!error) {
    for (const int fd : {fd0, fd1}) {
      if (get_fd(fd)) {
        /* We already have this fd, probably missed a close(). */
        exec_point()->disable_shortcutting_bubble_up(
            "Process created an fd which is known to be open, "
            "which means interception missed at least one close()", fd);
        handle_close(fd);
      }
      const int flags = O_RDWR
#ifdef SOCK_CLOEXEC
          | ((type & SOCK_CLOEXEC) ? O_CLOEXEC : 0)
#endif
#ifdef SOCK_NONBLOCK
          | ((type & SOCK_NONBLOCK) ? O_NONBLOCK : 0)
#endif
        | 0;
      add_filefd(fd, std::make_shared<FileFD>(flags, FD_SPECIAL, this));
    }
  } else {
    /* This is ulikely to happen and may not be deterministic .*/
    exec_point()->disable_shortcutting_bubble_up("socketpair() call failed");
  }
}

int Process::handle_dup3(const int oldfd, const int newfd, const int flags,
                         const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "oldfd=%d, newfd=%d, flags=%d, error=%d",
         oldfd, newfd, flags, error);

  switch (error) {
    case EBADF:
    case EBUSY:
    case EINTR:
    case EINVAL:
    case ENFILE: {
      /* dup() failed */
      return 0;
    }
    case 0:
    default:
      break;
  }

  /* validate fd-s */
  if (!get_fd(oldfd)) {
    /* we already have this fd, probably missed a close() */
    exec_point()->disable_shortcutting_bubble_up(
        "Process created an fd which is known to be open, "
        "which means interception missed at least one close()", oldfd);
    return -1;
  }

  /* dup2()'ing an existing fd to itself returns success and does nothing */
  if (oldfd == newfd) {
    return 0;
  }

  handle_force_close(newfd);

  add_filefd(newfd, std::make_shared<FileFD>((*fds_)[oldfd], flags & O_CLOEXEC));
  return 0;
}

int Process::handle_rename(const int olddirfd, const char * const old_ar_name,
                           const size_t old_ar_len,
                           const int newdirfd, const char * const new_ar_name,
                           const size_t new_ar_len,
                           const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "olddirfd=%d, old_ar_name=%s, newdirfd=%d, new_ar_name=%s, error=%d",
         olddirfd, D(old_ar_name), newdirfd, D(new_ar_name), error);

  /*
   * Note: rename() is different from "mv" in at least two aspects:
   *
   * - Can't move to a containing directory, e.g.
   *     rename("/home/user/file.txt", "/tmp");  (or ... "/tmp/")
   *   fails, you have to specify the full new name like
   *     rename("/home/user/file.txt", "/tmp/file.txt");
   *   instead.
   *
   * - If the source is a directory then the target can be an empty directory,
   *   which will be atomically removed beforehand. E.g. if "mytree" is a directory then
   *     mkdir("/tmp/target");
   *     rename("mytree", "/tmp/target");
   *   will result in the former "mytree/file.txt" becoming "/tmp/target/file.txt",
   *   with no "mytree" component. "target" has to be an empty directory for this to work.
   */

  const FileName* old_name = get_absolute(olddirfd, old_ar_name, old_ar_len);
  const FileName* new_name = get_absolute(newdirfd, new_ar_name, new_ar_len);

  /* Rename always sends pre_open, which pretends that the file was opened for writing. */
  if (old_name) old_name->close_for_writing();
  if (new_name) new_name->close_for_writing();

  if (error) {
    switch (error) {
      case EEXIST: {
        FileUsageUpdate update(new_name);
        update.set_initial_type(EXIST);
        if (!exec_point()->register_file_usage_update(new_name, update)) {
          exec_point()->disable_shortcutting_bubble_up(
              "Could not register the renaming (to)", *new_name);
          return -1;
        }
        return 0;
      }
      default:
        // TODO(rbalint) add detailed error handling
        exec_point()->disable_shortcutting_bubble_up("Failed rename() is not supported");
    }
    return -1;
  }

  if (!old_name || !new_name) {
    // FIXME don't disable shortcutting if renameat() failed due to the invalid dirfd
    exec_point()->disable_shortcutting_bubble_up("Invalid dirfd passed to renameat()");
    return -1;
  }

  struct stat64 st;
  if (lstat64(new_name->c_str(), &st) < 0 ||
      !S_ISREG(st.st_mode)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not register the renaming of non-regular file", *old_name);
    return -1;
  }

  /* It's tricky because the renaming has already happened, there's supposedly nothing
   * at the old filename. Yet we need to register that we read that file with its
   * particular hash value.
   * FIXME we compute the hash twice, both for the old and new location.
   * FIXME refactor so that it plays nicer together with register_file_usage_update(). */

  /* Register the opening for reading at the old location, although read the file's hash from the
   * new location. */
  FileUsageUpdate update_old =
      FileUsageUpdate::get_oldfile_usage_from_rename_params(old_name, new_name, error);
  if (!exec_point()->register_file_usage_update(old_name, update_old)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not register the renaming (from)", *old_name);
    return -1;
  }

  /* Register the opening for writing at the new location */
  // TODO(rbalint) fix error handling, it is way more complicated for rename than for open.
  FileUsageUpdate update_new =
      FileUsageUpdate::get_newfile_usage_from_rename_params(new_name, error);
  if (!exec_point()->register_file_usage_update(new_name, update_new)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Could not register the renaming (to)", *new_name);
    return -1;
  }

  return 0;
}

int Process::handle_symlink(const char * const target,
                            const int newdirfd, const char * const new_ar_name,
                            const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "target=%s, newdirfd=%d, new_ar_name=%s, error=%d",
         D(target), newdirfd, D(new_ar_name), error);

  if (!error) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process created a symlink",
        " ([" + d(newdirfd) + "]" + d(new_ar_name) + " -> " + d(target) + ")");
    return -1;
  }
  return 0;
}

int Process::handle_clear_cloexec(const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d", fd);

  if (!get_fd(fd)) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process successfully cleared cloexec on fd which is known to be closed, "
        "which means interception missed at least one open()", fd);
    return -1;
  }
  (*fds_)[fd]->set_cloexec(false);
  return 0;
}

int Process::handle_fcntl(const int fd, const int cmd, const int arg,
                          const int ret, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, cmd=%d, arg=%d, ret=%d, error=%d",
         fd, cmd, arg, ret, error);

  switch (cmd) {
    case F_DUPFD:
      return handle_dup3(fd, ret, 0, error);
    case F_DUPFD_CLOEXEC:
      return handle_dup3(fd, ret, O_CLOEXEC, error);
    case F_SETFD:
      if (error == 0) {
        if (!get_fd(fd)) {
          exec_point()->disable_shortcutting_bubble_up(
              "Process successfully fcntl'ed on fd which is known to be closed, "
              "which means interception missed at least one open()", fd);
          return -1;
        }
        (*fds_)[fd]->set_cloexec(arg & FD_CLOEXEC);
      }
      return 0;
#ifdef F_GETPATH
    case F_GETPATH:
      if (error == 0) {
        FileFD *file_fd = get_fd(fd);
        if (!file_fd) {
          exec_point()->disable_shortcutting_bubble_up(
              "Process successfully fcntl'ed on fd which is known to be closed, "
              "which means interception missed at least one open()", fd);
          return -1;
        } else {
          /* F_GETPATH was successful. It is OK, since the process can be shortcut only
           * with the process opening the file descriptor already. */
          return 0;
        }
      } else {
        /* F_GETPATH on an fd failed, this does not affect shortcutting. */
        return 0;
      }
#endif
    default:
      exec_point()->disable_shortcutting_bubble_up("Process executed unsupported fcntl ", d(cmd));
      return 0;
  }
}

int Process::handle_ioctl(const int fd, const int cmd,
                          const int ret, const int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, cmd=%d, ret=%d, error=%d",
         fd, cmd, ret, error);

  (void) ret;

  switch (cmd) {
    case FIOCLEX:
      if (error == 0) {
        if (!get_fd(fd)) {
          exec_point()->disable_shortcutting_bubble_up(
              "Process successfully ioctl'ed on fd which is known to be closed, "
              "which means interception missed at least one open()", fd);
          return -1;
        }
        (*fds_)[fd]->set_cloexec(true);
      }
      return 0;
    case FIONCLEX:
      if (error == 0) {
        if (!get_fd(fd)) {
          exec_point()->disable_shortcutting_bubble_up(
              "Process successfully ioctl'ed on fd which is known to be closed, "
              "which means interception missed at least one open()", fd);
          return -1;
        }
        (*fds_)[fd]->set_cloexec(false);
      }
      return 0;
    default:
      exec_point()->disable_shortcutting_bubble_up("Process executed unsupported ioctl",
                                                   " " + d(cmd));
      return 0;
  }
}

void Process::handle_read_from_inherited(const int fd, const bool is_pread) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, is_pread=%s", fd, D(is_pread));

  (void)is_pread;  /* unused */

  FileFD *file_fd = get_fd(fd);
  if (!file_fd) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process successfully read from fd which is known to be closed, which means interception"
        " missed at least one open()", fd);
    return;
  } else if (file_fd->type() == FD_PIPE_IN
             && file_fd->pipe() && fd == exec_point()->jobserver_fd_r()) {
    // TODO(rbalint) add further check that the process chain did not reopen the fd breaking the
    // connection to the jobserver
    /* It is OK to read from the jobserver. It should not affect the build results. */
    return;
  }

  const ExecedProcess *file_fd_opened_by_exec_point =
      file_fd->opened_by() ? file_fd->opened_by()->exec_point() : nullptr;
  if (file_fd_opened_by_exec_point == exec_point()) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process sent handle_read_from_inherited for a non-inherited fd", fd);
    return;
  }

  if (file_fd->type() == FD_IGNORED) {
    return;
  }

  Process* opened_by = file_fd->opened_by();
  exec_point()->disable_shortcutting_bubble_up_to_excl(
      opened_by ? opened_by->exec_point() : nullptr, "Process read from inherited fd ", fd);
}

void Process::handle_write_to_inherited(const int fd, const bool is_pwrite) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, is_pwrite=%s", fd, D(is_pwrite));

  FileFD *file_fd = get_fd(fd);
  if (!file_fd) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process successfully wrote to fd which is known to be closed, which means interception"
        " missed at least one open()", fd);
    return;
  } else if (file_fd->type() == FD_PIPE_OUT
             && file_fd->pipe() && fd == exec_point()->jobserver_fd_w()) {
    // TODO(rbalint) add further check that the process chain did not reopen the fd breaking the
    // connection to the jobserver
    /* It is OK to write to the jobserver, but jobserver communication should not be cached. */
    return;
  }

  const ExecedProcess *file_fd_opened_by_exec_point =
      file_fd->opened_by() ? file_fd->opened_by()->exec_point() : nullptr;
  if (file_fd_opened_by_exec_point == exec_point()) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process sent handle_write_to_inherited for a non-inherited fd", fd);
    return;
  }

  if (file_fd->type() == FD_IGNORED) {
    return;
  }

  if (is_pwrite) {
    Process* opened_by = file_fd->opened_by();
    exec_point()->disable_shortcutting_bubble_up_to_excl(
        opened_by ? opened_by->exec_point() : nullptr,
        "Process called pwrite() on inherited fd ", fd);
  } else if (!file_fd->pipe() && !file_fd->filename()) {
    Process* opened_by = file_fd->opened_by();
    exec_point()->disable_shortcutting_bubble_up_to_excl(
        opened_by ? opened_by->exec_point() : nullptr,
        "Process wrote to inherited non-pipe and non-file fd ", fd);
  }
}

void Process::handle_seek_in_inherited(const int fd, const bool modify_offset) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, modify_offset=%s", fd, D(modify_offset));

  FileFD *file_fd = get_fd(fd);
  if (!file_fd) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process successfully seeked in an fd which is known to be closed, which means interception"
        " missed at least one open()", fd);
    return;
  }

  const ExecedProcess *file_fd_opened_by_exec_point =
      file_fd->opened_by() ? file_fd->opened_by()->exec_point() : nullptr;
  if (file_fd_opened_by_exec_point == exec_point()) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process sent handle_seek_in_inherited for a non-inherited fd", fd);
    return;
  }

  if (file_fd->type() == FD_IGNORED) {
    return;
  }

  // FIXME Handle the !modify_offset case
  if (modify_offset && !file_fd->pipe()) {
    Process* opened_by = file_fd->opened_by();
    exec_point()->disable_shortcutting_bubble_up_to_excl(
        opened_by ? opened_by->exec_point() : nullptr,
        "Process seeked in inherited non-pipe fd ", fd);
  }
}

void Process::handle_inherited_fd_offset(const int fd, const int64_t offset) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d, offset==%" PRId64, fd, offset);
  FileFD *file_fd = exec_point()->get_fd(fd);
  if (!file_fd) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process reported offset for an intercepted seekable fd which is no known to be open", fd);
    return;
  }
  const int flags = file_fd->flags() & O_ACCMODE;
  if (flags == O_WRONLY || flags == O_RDWR) {
    exec_point()->disable_shortcutting_only_this(
        "Inherited writable non-append fd not seeked to its end");
  }
  for (inherited_file_t& inherited_file : exec_point()->inherited_files()) {
    if (inherited_file.fds[0] == fd) {
      inherited_file.start_offset = offset;
      break;
    }
  }
}

void Process::handle_recvmsg_scm_rights(const bool cloexec, const std::vector<int> fds) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "cloexec=%s fds=%s", D(cloexec), D(fds));

  for (int fd : fds) {
    FileFD *file_fd = get_fd(fd);
    if (file_fd) {
      exec_point()->disable_shortcutting_bubble_up(
        "Process successfully received fd via SCM_RIGHTS which is known to be open, which means"
        " interception missed at least one close()", fd);
    } else {
      add_filefd(fd, std::make_shared<FileFD>(cloexec ? O_CLOEXEC : 0, FD_SCM_RIGHTS, this));
    }
  }

  exec_point()->disable_shortcutting_bubble_up("Receiving an fd via SCM_RIGHTS is not supported");
}


void Process::handle_set_wd(const char * const ar_d, const size_t ar_d_len) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "ar_d=%s", ar_d);

  wd_ = get_absolute(AT_FDCWD, ar_d, ar_d_len);
  assert(wd_);
  add_wd(wd_);
}

void Process::handle_set_fwd(const int fd) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "fd=%d", fd);

  const FileFD* ffd = get_fd(fd);
  if (!ffd) {
    exec_point()->disable_shortcutting_bubble_up(
        "Process successfully fchdir()'ed to an  fd which is known to be closed, which means "
        "interception missed at least one open()", fd);
    return;
  }
  wd_ = ffd->filename();
  assert(wd_);
  add_wd(wd_);
}

void Process::handle_umask(mode_t old_umask, mode_t new_umask) {
  if (umask() != old_umask) {
    exec_point()->disable_shortcutting_bubble_up("Old umask mismatches, which means "
                                                 "interception missed at least one umask()");
  }
  umask_ = new_umask & 0777;
}

const FileName* Process::get_fd_filename(int fd) const {
  if (fd == AT_FDCWD) {
    return wd();
  } else {
    const FileFD* ffd = get_fd(fd);
    if (ffd) {
      return ffd->filename();
    } else {
      return nullptr;
    }
  }
}

const FileName* Process::get_absolute(const int dirfd, const char * const name,
                                      ssize_t length) const {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "dirfd=%d, name=%s, length=%" PRIssize,
         dirfd, D(name), length);

  if (path_is_absolute(name)) {
    return FileName::Get(name, length);
  } else {
    char on_stack_buf[2048], *buf;

    const FileName* dir = get_fd_filename(dirfd);
    if (!dir) {
      return nullptr;
    }

    const ssize_t name_length = (length == -1) ? strlen(name) : length;
    /* Both dir and name are in canonical form thanks to the interceptor, only "." and ""
     * need handling here. See make_canonical() in the interceptor. */
    if (name_length == 0 || (name_length == 1 && name[0] == '.')) {
      return dir;
    }

    const size_t on_stack_buffer_size = sizeof(on_stack_buf);
    const bool separator_needed = (dir->c_str()[dir->length() - 1] != '/');
    /* Only "/" should end with a separator */
    assert(separator_needed || dir->length() == 1);
    const size_t total_buf_len = dir->length() + (separator_needed ? 1 : 0) + name_length + 1;
    if (on_stack_buffer_size < total_buf_len) {
      buf = reinterpret_cast<char *>(malloc(total_buf_len));
    } else {
      buf = reinterpret_cast<char *>(on_stack_buf);
    }

    memcpy(buf, dir->c_str(), dir->length());
    size_t offset = dir->length();
    if (separator_needed) {
      buf[offset++] = '/';
    }

    memcpy(buf + offset, name, name_length);
    buf[total_buf_len - 1] = '\0';
    const FileName* ret = FileName::Get(buf, total_buf_len - 1);
    if (on_stack_buffer_size < total_buf_len) {
      free(buf);
    }
    return ret;
  }
}

static bool argv_match_from_offset(const std::vector<std::string>& actual,
                                   const int actual_offset,
                                   const std::vector<std::string>& expected,
                                   const int expected_offset) {
  /* For the remaining parameters exact match is needed. */
  if (actual.size() - actual_offset != expected.size() - expected_offset) {
    return false;
  }
  for (unsigned int i = expected_offset; i < expected.size(); i++) {
    if (actual[actual_offset + i - expected_offset] != expected[expected_offset]) {
      return false;
    }
  }
  return true;
}

static bool argv_matches_expectation(const std::vector<std::string>& actual,
                                     const std::vector<std::string>& expected) {
  /* When launching ["foo", "arg1"], the new process might be something like
   * ["/bin/bash", "-e", "./foo", "arg1"].
   *
   * If the program name already contained a slash, or if it refers to a binary executable,
   * it remains unchanged. If it didn't contain a slash and it refers to an interpreted file,
   * it's replaced by an absolute or relative path containing at least one slash (e.g. "./foo").
   *
   * A single interpreter might only add one additional command line parameter, but there can be
   * a chain of interpreters, so allow for arbitrarily many prepended parameters.
   */

  if (actual.size() == expected.size()) {
    /* If the length is the same, exact match is required. */
    return actual == expected;
  }

  if (actual.size() > expected.size()) {
    /* If the length grew, check the expected arg0. */
    int offset = actual.size() - expected.size();
    if (expected[0].find('/') != std::string::npos) {
      /* If it contained a slash, exact match is needed. */
      if (actual[offset] != expected[0]) {
        return false;
      }
    } else {
      /* If it didn't contain a slash, it needed to get prefixed with some path. */
      int expected_arg0_len = expected[0].length();
      int actual_arg0_len = actual[offset].length();
      /* Needs to be at least 1 longer. */
      if (actual_arg0_len <= expected_arg0_len) {
        return false;
      }
      /* See if the preceding character is a '/'. */
      if (actual[offset][actual_arg0_len - expected_arg0_len - 1] != '/') {
        return false;
      }
      /* See if the stuff after the '/' is the same. */
      if (actual[offset].compare(actual_arg0_len - expected_arg0_len,
                                 expected_arg0_len, expected[0]) != 0) {
        return false;
      }
    }

    return argv_match_from_offset(actual, offset + 1, expected, 1);
  } else {
    /* Actual argv is shorter than expected. */
#if !FB_GLIBC_PREREQ (2, 38)
    /* Older libc-s does not pass "--" between "sh -c" and system() and popen() argument. */
    if (expected.size() > 2 && expected[2] == "--" && actual.size() > 2 && actual[2] != "--" &&
        expected[0] == "sh" && expected[1] == "-c" && actual[0] == "sh" && actual[1] == "-c") {
      return argv_match_from_offset(actual, 3, expected, 4);
    }
#endif
    return false;
  }
}

std::vector<std::shared_ptr<FileFD>>*
Process::pop_expected_child_fds(const std::vector<std::string>& argv,
                                LaunchType *launch_type_p,
                                int *type_flags_p,
                                const bool failed) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "failed=%s", D(failed));

  if (expected_child_) {
    const LaunchType child_launch_type = expected_child_->launch_type();
    if ((child_launch_type == LAUNCH_TYPE_SYSTEM && expected_child_->argv().size() == 0)
        || (argv_matches_expectation(argv, expected_child_->argv()))) {
      auto fds = expected_child_->pop_fds();
      if (launch_type_p)
          *launch_type_p = child_launch_type;
      if (type_flags_p)
          *type_flags_p = expected_child_->type_flags();
      delete(expected_child_);
      expected_child_ = nullptr;
      return fds;
    } else {
      exec_point()->disable_shortcutting_bubble_up(
          "Unexpected system/popen/posix_spawn child appeared", ": " + d(argv) + " "
          "while waiting for: " + d(expected_child_));
    }
    delete(expected_child_);
    expected_child_ = nullptr;
  } else {
    exec_point()->disable_shortcutting_bubble_up("Unexpected system/popen/posix_spawn child",
                                   std::string(failed ? "  failed: " : " appeared: ") + d(argv));
  }
  return nullptr;
}

bool Process::finalized_or_terminated_and_has_orphan_and_finalized_children() const {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (state() == FB_PROC_FINALIZED) {
    return true;
  } else if (state() == FB_PROC_RUNNING) {
    return false;
  }
  assert_cmp(state(), ==, FB_PROC_TERMINATED);

  if (fork_point()->has_orphan_descendant()) {
    for (ForkedProcess* fork_child : fork_children()) {
      if (!fork_child->orphan()
          && !fork_child->finalized_or_terminated_and_has_orphan_and_finalized_children()) {
        return false;
      }
    }

    /* There can be orphan processes with terminated parents which were not orphan.
     * Those are not finalized until the last process in the chain terminates, but should be
     * treated as orphan processes.*/
    return exec_child() ?
        exec_child()->finalized_or_terminated_and_has_orphan_and_finalized_children() : true;
  } else {
    return false;
  }
}

bool Process::any_child_not_finalized() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (exec_pending_ || has_pending_popen()) {
    return true;
  }
  if (exec_child() && exec_child()->state() != FB_PROC_FINALIZED) {
    /* The exec child is not yet finalized. We're not ready to finalize either. */
    return true;
  }

  for (auto fork_child : fork_children_) {
    if (fork_child->state_ != FB_PROC_FINALIZED) {
      return true;
    }
  }
  return false;
}

bool Process::any_child_not_finalized_or_terminated_with_orphan() const {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (exec_pending_ || has_pending_popen()) {
    return true;
  }

  if (exec_child()
      && !exec_child()->finalized_or_terminated_and_has_orphan_and_finalized_children()) {
    return true;
  }

  for (auto fork_child : fork_children_) {
    if (!fork_child->finalized_or_terminated_and_has_orphan_and_finalized_children()) {
      return true;
    }
  }
  return false;
}

/**
 * Terminate orphan descendant processes which have only terminated ancestors.
 *
 * Those are likely the ones which are not terminated by their parents
 * thus would be left behind by the build.
 *
 * Note: This function should be called on the top (execed) process of the build to
 * clean up all orphans.
 */
void Process::terminate_top_orphans() const {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");
  if (fork_point()->orphan() && state() == FB_PROC_RUNNING) {
    /* If the supervisor is a subreaper and there is no subreaper among the supervised processes,
     * then it is safe to assume that all orphans are still running or are zombies waiting for being
     * reaped, thus they can be kill()-ed by pid. */
    FB_DEBUG(FB_DEBUG_PROC, "Killing top orphan process " + d(this));
    kill(pid(), SIGTERM);
    /* Continue with all fork children of this exec chain. The processes of this exec chain are
     * not kill()-ed again. */
    const Process* curr = this;
    do {
      for (const ForkedProcess* child : curr->fork_children()) {
        child->terminate_top_orphans();
      }
      curr = curr->exec_child();
    } while (curr);
  }
  if (state() == FB_PROC_TERMINATED) {
    for (const ForkedProcess* child : fork_children()) {
      child->terminate_top_orphans();
    }
    if (exec_child()) {
      exec_child()->terminate_top_orphans();
    }
  }
}

/**
 * Finalize the current process.
 */
void Process::do_finalize() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  assert_cmp(state(), ==, FB_PROC_TERMINATED);
  set_state(FB_PROC_FINALIZED);
}

/**
 * Finalize the current process if possible, and if successful then
 * bubble it up.
 */
void Process::maybe_finalize() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (state() != FB_PROC_TERMINATED) {
    return;
  }
  if (any_child_not_finalized()) {
    if (!fork_point()->parent()
        && fork_point()->has_orphan_descendant()
        && finalized_or_terminated_and_has_orphan_and_finalized_children()) {
      /* Kill all orphan processes when the root exec process can't be finalized because of them.
       * They may or may not quit on their own, but it is impossible to tell. */
      fork_point()->terminate_top_orphans();
      /* This top exec process can now be finalized, because no ancestor of the just terminated
       * orphans would be cached. The supervisor will exit after the descendants of the orphans
       * terminate, too. Otherwise if we return from this function here the supervisor would quickly
       * kill all the descendants of the just killed orphans because those became orphans, too. */
    } else {
      /* We're not ready to finalize. */
      return;
    }
  }

  /* Only finalize the process after it is clear that if the parent has waited for it. */
  // TODO(rbalint) collect/confirm exit status in the parent to make sure that the exit
  // status saved in the cache will be correct.
  if (!been_waited_for()) {
    const Process* fork_parent_ptr = fork_parent();
    if (fork_parent_ptr) {
      const Process* potential_waiter = fork_parent_ptr->last_exec_descendant();
      if (potential_waiter->state() != FB_PROC_RUNNING && !potential_waiter->exec_pending()) {
        /* The process that could wait for this one is not running anymore. */
        fork_point()->set_orphan();
        exec_point()->disable_shortcutting_bubble_up("Orphan processes can't be shortcut",
                                                     exec_point());
        fork_parent()->fork_point()->set_has_orphan_descendant_bubble_up();
        /* Can proceed with finalizing this process, it won't be saved to the cache. */
      } else {
        /* Wait for the fork parent to wait() for this child or to terminate. */
        return;
      }
    } else {
      /* The top process will be waited for later. */
      return;
    }
  }

  drain_all_pipes();
  do_finalize();

  if (parent()) {
    parent()->maybe_finalize();
  }
}

void Process::finish() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (FB_DEBUGGING(FB_DEBUG_PROC) && expected_child_) {
    FB_DEBUG(FB_DEBUG_PROC, "Expected system()/popen()/posix_spawn() children that did not appear"
                            " (e.g. posix_spawn() failed in the pre-exec or exec step):");
    FB_DEBUG(FB_DEBUG_PROC, "  " + d(expected_child_));
  }

  if (!exec_pending_ && !has_pending_popen()) {
    /* The pending child may show up and it needs to inherit the pipes. */
    reset_file_fd_pipe_refs();
  }

  set_state(FB_PROC_TERMINATED);
  /* Here we check only the fork children. In theory this parent could exec() and the
   * execed process could wait for its children, but this is rare and costly to detect thus
   * we disable shortcutting in more cases than it is absolutely needed.
   */
  if (!exec_child() && !exec_pending()) {
    /* This is the last process in the exec chain. Let's see if orphans were left behind. */
    for (Process* curr = fork_point(); curr; curr = curr->exec_child()) {
      bool orphan_found = false;
      for (ForkedProcess* fork_child : curr->fork_children()) {
        if (!fork_child->been_waited_for()) {
          /* This may also be set in last_exec_descendant->maybe_finalize(), but not when
           * last_exec_descendant has not finalized children. */
          fork_child->set_orphan();
          orphan_found = true;
          /* This disables shortcutting the fork child and maybe finalizes it. Since shortcutting is
           * disabled up to the top process this process will not be shortcuttable either. */
          fork_child->last_exec_descendant()->maybe_finalize();
        }
      }
      if (orphan_found) {
        curr->exec_point()->disable_shortcutting_bubble_up("Orphan processes can't be shortcut",
                                                           exec_point());
        curr->fork_point()->set_has_orphan_descendant_bubble_up();
      }
    }
  }

  maybe_finalize();
}

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string Process::d_internal(const int level) const {
  (void)level;  /* unused */
  return "{Process " + pid_and_exec_count() + "}";
}

Process::~Process() {
  TRACKX(FB_DEBUG_PROC, 1, 0, Process, this, "");

  delete(expected_child_);
  delete(fds_);
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Process& p, const int level) {
  return p.d_internal(level);
}
std::string d(const Process *p, const int level) {
  if (p) {
    return d(*p, level);
  } else {
    return "{Process NULL}";
  }
}

}  /* namespace firebuild */
