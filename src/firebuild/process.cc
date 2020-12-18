/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "firebuild/file.h"
#include "firebuild/platform.h"
#include "firebuild/execed_process.h"
#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"
#include "firebuild/utils.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const FileName *wd,
                 Process * parent, std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds)
    : parent_(parent), state_(FB_PROC_RUNNING), fb_pid_(fb_pid_counter++),
      pid_(pid), ppid_(ppid), exit_status_(-1), wd_(wd), fds_(fds),
      closed_fds_({}), utime_u_(0), stime_u_(0), aggr_time_(0), children_(),
      expected_child_(), exec_child_(NULL) {
  if (!fds_) {
    fds_ = std::make_shared<std::vector<std::shared_ptr<FileFD>>>();
    add_filefd(fds_, STDIN_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY));
    add_filefd(fds_, STDOUT_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY));
    add_filefd(fds_, STDERR_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY));
  }
}

void Process::update_rusage(const int64_t utime_u, const int64_t stime_u) {
  utime_u_ = utime_u;
  stime_u_ = stime_u;
}

void Process::exit_result(const int status, const int64_t utime_u,
                          const int64_t stime_u) {
  /* The kernel only lets the low 8 bits of the exit status go through.
   * From the exit()/_exit() side, the remaining bits are lost (they are
   * still there in on_exit() handlers).
   * From wait()/waitpid() side, additional bits are used to denote exiting
   * via signal.
   * We use -1 if there's no exit status available (the process is still
   * running, or exited due to an unhandled signal). */
  exit_status_ = status & 0xff;
  update_rusage(utime_u, stime_u);
}

void Process::sum_rusage(int64_t * const sum_utime_u,
                         int64_t *const sum_stime_u) {
  (*sum_utime_u) += utime_u_;
  (*sum_stime_u) += stime_u_;
  for (unsigned int i = 0; i < children_.size(); i++) {
    children_[i]->sum_rusage(sum_utime_u, sum_stime_u);
  }
}

void Process::add_filefd(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds,
                         int fd, std::shared_ptr<FileFD> ffd) {
  if (fds->size() <= static_cast<unsigned int>(fd)) {
    fds->resize(fd + 1, nullptr);
  }
  if ((*fds)[fd]) {
    firebuild::fb_error("Fd " + std::to_string(fd) + " is already tracked as being open.");
  }
  // the shared_ptr takes care of cleaning up the old fd if needed
  (*fds)[fd] = ffd;
}

std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> Process::pass_on_fds(bool execed) {
  auto fds = std::make_shared<std::vector<std::shared_ptr<FileFD>>>();
  for (unsigned int i = 0; i < fds_->size(); i++) {
    if (fds_->at(i) && !(execed &&(*fds_)[i]->cloexec())) {
      add_filefd(fds, i, std::make_shared<FileFD>(*fds_->at(i).get()));
    }
  }
  return fds;
}

int Process::handle_open(const int dirfd, const char * const ar_name, const int flags,
                         const int fd, const int error, int fd_conn, const int ack_num) {
  const FileName* name = get_absolute(dirfd, ar_name);
  if (!name) {
    // FIXME don't disable shortcutting if openat() failed due to the invalid dirfd
    disable_shortcutting_bubble_up("Invalid dirfd passed to openat()");
    return -1;
  }

  if (fd >= 0) {
    add_filefd(fds_, fd, std::make_shared<FileFD>(name, fd, flags, this));
  }

  if (ack_num != 0) {
    ack_msg(fd_conn, ack_num);
  }

  if (!exec_point()->register_file_usage(name, name, FILE_ACTION_OPEN, flags, error)) {
    disable_shortcutting_bubble_up("Could not register the opening of " +
                                   pretty_print_string(name->to_string()));
    return -1;
  }

  return 0;
}

/* close that fd if open, silently ignore if not open */
int Process::handle_force_close(const int fd) {
  if (get_fd(fd)) {
    return handle_close(fd, 0);
  }
  return 0;
}

int Process::handle_close(const int fd, const int error) {
  if (error == EIO) {
    // IO prevents shortcutting
    disable_shortcutting_bubble_up("IO error closing fd " + std::to_string(fd));
    return -1;
  } else if (error == 0 && !get_fd(fd)) {
    // closing an unknown fd successfully prevents shortcutting
    disable_shortcutting_bubble_up("Process closed an unknown fd (" +
                                   std::to_string(fd) + ") successfully, which means "
                                   "interception missed at least one open()");
    return -1;
  } else if (error == EBADF) {
    // Process closed an fd unknown to it. Who cares?
    return 0;
  } else if (!get_fd(fd)) {
    // closing an unknown fd with not EBADF prevents shortcutting
    disable_shortcutting_bubble_up("Process closed an unknown fd (" +
                                   std::to_string(fd) + ") successfully, which means "
                                   "interception missed at least one open()");
    return -1;
  } else {
    if ((*fds_)[fd]->open() == true) {
      (*fds_)[fd]->set_open(false);
      if ((*fds_)[fd]->last_err() != error) {
        (*fds_)[fd]->set_last_err(error);
      }
      // remove from open fds
      closed_fds_.push_back((*fds_)[fd]);
      (*fds_)[fd].reset();
      return 0;
    } else if (((*fds_)[fd]->last_err() == EINTR) && (error == 0)) {
      // previous close got interrupted but the current one succeeded
      (*fds_)[fd].reset();
      return 0;
    } else {
      // already closed, it may be an error
      // TODO(rbalint) debug
      (*fds_)[fd].reset();
      return 0;
    }
  }
}

int Process::handle_unlink(const int dirfd, const char * const ar_name,
                           const int flags, const int error) {
  const FileName* name = get_absolute(dirfd, ar_name);
  if (!name) {
    // FIXME don't disable shortcutting if unlinkat() failed due to the invalid dirfd
    disable_shortcutting_bubble_up("Invalid dirfd passed to unlinkat()");
    return -1;
  }

  if (!error) {
    // FIXME When a directory is removed, register that it was an _empty_ directory
    FileUsage fu(flags & AT_REMOVEDIR ? ISDIR : ISREG);
    fu.set_written(true);
    if (!exec_point()->register_file_usage(name, fu)) {
      disable_shortcutting_bubble_up("Could not register the unlink or rmdir of " +
                                     pretty_print_string(name));
      return -1;
    }
  }

  return 0;
}

int Process::handle_rmdir(const char * const ar_name, const int error) {
  return handle_unlink(AT_FDCWD, ar_name, AT_REMOVEDIR, error);
}

int Process::handle_mkdir(const int dirfd, const char * const ar_name, const int error) {
  const FileName* name = get_absolute(dirfd, ar_name);
  if (!name) {
    // FIXME don't disable shortcutting if mkdirat() failed due to the invalid dirfd
    disable_shortcutting_bubble_up("Invalid dirfd passed to mkdirat()");
    return -1;
  }

  if (!exec_point()->register_file_usage(name, name, FILE_ACTION_MKDIR, 0, error)) {
    disable_shortcutting_bubble_up("Could not register the directory creation of " +
                                   pretty_print_string(name));
    return -1;
  }

  return 0;
}

int Process::handle_pipe(const int fd1, const int fd2, const int flags,
                         const int error) {
  if (error) {
    return 0;
  }

  // validate fd-s
  if (get_fd(fd1)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting_bubble_up("Process created an fd (" + std::to_string(fd1) +
                                   ") which is known to be open, which means interception "
                                   "missed at least one close()");
    return -1;
  }
  if (get_fd(fd2)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting_bubble_up("Process created an fd (" + std::to_string(fd2) +
                                   ") which is known to be open, which means interception "
                                   "missed at least one close()");
    return -1;
  }

  add_filefd(fds_, fd1, std::make_shared<FileFD>(
      fd1, (flags & ~O_ACCMODE) | O_RDONLY, FD_ORIGIN_PIPE, this));
  add_filefd(fds_, fd2, std::make_shared<FileFD>(
      fd2, (flags & ~O_ACCMODE) | O_WRONLY, FD_ORIGIN_PIPE, this));
  return 0;
}

int Process::handle_dup3(const int oldfd, const int newfd, const int flags,
                         const int error) {
  switch (error) {
    case EBADF:
    case EBUSY:
    case EINTR:
    case EINVAL:
    case ENFILE: {
      // dup() failed
      return 0;
    }
    case 0:
    default:
      break;
  }

  // validate fd-s
  if (!get_fd(oldfd)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting_bubble_up("Process created an fd (" + std::to_string(oldfd) +
                                   ") which is known to be open, which means interception"
                                   " missed at least one close()");
    return -1;
  }

  // dup2()'ing an existing fd to itself returns success and does nothing
  if (oldfd == newfd) {
    return 0;
  }

  handle_force_close(newfd);

  add_filefd(fds_, newfd, std::make_shared<FileFD>(
      newfd, (((*fds_)[oldfd]->flags() & ~O_CLOEXEC) | flags), FD_ORIGIN_DUP,
      (*fds_)[oldfd]));
  return 0;
}

int Process::handle_rename(const int olddirfd, const char * const old_ar_name,
                           const int newdirfd, const char * const new_ar_name,
                           const int error) {
  if (error) {
    return 0;
  }
  const FileName* old_name = get_absolute(olddirfd, old_ar_name);
  const FileName* new_name = get_absolute(newdirfd, new_ar_name);
  if (!old_name || !new_name) {
    // FIXME don't disable shortcutting if renameat() failed due to the invalid dirfd
    disable_shortcutting_bubble_up("Invalid dirfd passed to renameat()");
    return -1;
  }

  /* It's tricky because the renaming has already happened, there's supposedly nothing
   * at the old filename. Yet we need to register that we read that file with its
   * particular hash value.
   * FIXME we compute the hash twice, both for the old and new location.
   * FIXME refactor so that it plays nicer together with register_file_usage(). */

  /* Register the opening for reading at the old location */
  if (!exec_point()->register_file_usage(old_name, new_name, FILE_ACTION_OPEN, O_RDONLY, error)) {
    disable_shortcutting_bubble_up("Could not register the renaming from " +
                                   pretty_print_string(old_name));
    return -1;
  }

  /* Register the opening for writing at the new location */
  if (!exec_point()->register_file_usage(new_name, new_name,
                                         FILE_ACTION_OPEN, O_CREAT|O_WRONLY|O_TRUNC, error)) {
    disable_shortcutting_bubble_up("Could not register the renaming to " +
                                   pretty_print_string(new_name));
    return -1;
  }

  return 0;
}

int Process::handle_symlink(const char * const old_ar_name,
                            const int newdirfd, const char * const new_ar_name,
                            const int error) {
  if (!error) {
    disable_shortcutting_bubble_up("Process created a symlink ([" + std::to_string(newdirfd) + "]" +
                                   pretty_print_string(new_ar_name) + " -> " +
                                   pretty_print_string(old_ar_name) + ")");
    return -1;
  }
  return 0;
}

int Process::handle_clear_cloexec(const int fd) {
  if (!get_fd(fd)) {
    disable_shortcutting_bubble_up("Process successfully cleared cloexec on fd (" +
                                   std::to_string(fd) +
                                   ") which is known to be closed, which means interception"
                                   " missed at least one open()");
    return -1;
  }
  (*fds_)[fd]->set_cloexec(false);
  return 0;
}

int Process::handle_fcntl(const int fd, const int cmd, const int arg,
                          const int ret, const int error) {
  switch (cmd) {
    case F_DUPFD:
      return handle_dup3(fd, ret, 0, error);
    case F_DUPFD_CLOEXEC:
      return handle_dup3(fd, ret, O_CLOEXEC, error);
    case F_SETFD:
      if (error == 0) {
        if (!get_fd(fd)) {
          disable_shortcutting_bubble_up("Process successfully fcntl'ed on fd (" +
                                         std::to_string(fd) +
                                         ") which is known to be closed, which means interception"
                                         " missed at least one open()");
          return -1;
        }
        (*fds_)[fd]->set_cloexec(arg & FD_CLOEXEC);
      }
      return 0;
    default:
      disable_shortcutting_bubble_up("Process executed unsupported fcntl " + std::to_string(cmd));
      return 0;
  }
}

int Process::handle_ioctl(const int fd, const int cmd,
                          const int ret, const int error) {
  (void) ret;

  switch (cmd) {
    case FIOCLEX:
      if (error == 0) {
        if (!get_fd(fd)) {
          disable_shortcutting_bubble_up("Process successfully ioctl'ed on fd (" +
                                         std::to_string(fd) +
                                         ") which is known to be closed, which means interception"
                                         " missed at least one open()");
          return -1;
        }
        (*fds_)[fd]->set_cloexec(true);
      }
      return 0;
    case FIONCLEX:
      if (error == 0) {
        if (!get_fd(fd)) {
          disable_shortcutting_bubble_up("Process successfully ioctl'ed on fd (" +
                                         std::to_string(fd) +
                                         ") which is known to be closed, which means interception"
                                         " missed at least one open()");
          return -1;
        }
        (*fds_)[fd]->set_cloexec(false);
      }
      return 0;
    default:
      disable_shortcutting_bubble_up("Process executed unsupported ioctl " + std::to_string(cmd));
      return 0;
  }
}

void Process::handle_read(const int fd) {
  if (!get_fd(fd)) {
    disable_shortcutting_bubble_up("Process successfully read from (" +
                                   std::to_string(fd) +
                                   ") which is known to be closed, which means interception"
                                   " missed at least one open()");
    return;
  }
  /* Note: this doesn't disable any shortcutting if (*fds_)[fd]->opened_by() == this,
   * i.e. the file was opened by the current process. */
  disable_shortcutting_bubble_up_to_excl((*fds_)[fd]->opened_by(),
                                         "Process read from inherited fd " + std::to_string(fd));
}

void Process::handle_write(const int fd) {
  if (!get_fd(fd)) {
    disable_shortcutting_bubble_up("Process successfully wrote to (" +
                                   std::to_string(fd) +
                                   ") which is known to be closed, which means interception"
                                   " missed at least one open()");
    return;
  }
  /* Note: this doesn't disable any shortcutting if (*fds_)[fd]->opened_by() == this,
   * i.e. the file was opened by the current process. */
  disable_shortcutting_bubble_up_to_excl((*fds_)[fd]->opened_by(),
                                         "Process wrote to inherited fd " + std::to_string(fd));
}

void Process::handle_set_wd(const char * const ar_d) {
  wd_ = get_absolute(AT_FDCWD, ar_d);
  assert(wd_);
  add_wd(wd_);
}

void Process::handle_set_fwd(const int fd) {
  std::shared_ptr<FileFD> ffd = get_fd(fd);
  if (!ffd) {
    disable_shortcutting_bubble_up("Process successfully fchdir()'ed to (" +
                                   std::to_string(fd) +
                                   ") which is known to be closed, which means interception"
                                   " missed at least one open()");
    return;
  }
  wd_ = ffd->filename();
  assert(wd_);
  add_wd(wd_);
}

/**
 * Canonicalize the filename in place.
 *
 * String operation only, does not look at the actual file system.
 * Removes double slashes, trailing slashes (except if the entire path is "/")
 * and "." components.
 * Preserves ".." components, since they might point elsewhere if a symlink led to
 * its containing directory.
 * See #401 for further details and gotchas.
 *
 * Returns the length of the canonicalized path.
 */
static inline size_t canonicalize_path(char *path) {
  char *src = path, *dst = path;  /* dst <= src all the time */
  bool add_slash = true;

  if (path[0] == '\0') return 0;

  if (!(path[0] == '.' && path[1] == '/')) {
    char *a = strstr(path, "//");
    char *b = strstr(path, "/./");
    if (a == NULL && b == NULL) {
      /* This is the quick code path for most of the well-behaved paths:
       * doesn't start with "./", doesn't contain "//" or "/./".
       * If a path passes this check then the only thing that might need
       * fixing is a trailing "/" or "/.". */
      int len = strlen(path);
      if (len >= 2 && path[len - 1] == '.' && path[len - 2] == '/') {
        /* Strip the final "." if the path ends in "/.". */
        len--;
        path[len] = '\0';
      }
      if (len >= 2 && path[len - 1] == '/') {
        /* Strip the final "/" if the path ends in "/" and that's not the entire path. */
        len--;
        path[len] = '\0';
      }
      /* The quick code path is done here. */
      return len;
    }
    /* Does not start with "./", but contains at least a "//" or "/./".
     * Everything is fine up to that point. Fast forward src and dst. */
    if (a != NULL && b != NULL) {
      src = dst = a < b ? a : b;
    } else if (a != NULL) {
      src = dst = a;
    } else {
      src = dst = b;
    }
  } else {
    /* Starts with "./", needs fixing from the beginning. */
    src++;
    add_slash = false;  /* Don't add "/" to dst when skipping the first one(s) in src. */
  }

  while (src[0] != '\0') {
    /* Skip through a possible run of slashes and non-initial "." components, e.g. "//././". */
    if (src[0] == '/') {
      while (src[0] == '/' || (src[0] == '.' && (src[1] == '/' || src[1] == '\0'))) src++;
      if (add_slash) {
        *dst++ = '/';
      }
    }
    /* Handle a regular (not ".") component. */
    while (src[0] != '/' && src[0] != '\0') {
      *dst++ = *src++;
    }
    add_slash = true;
  }

  /* If got empty path then it should be a "." instead. */
  if (dst == path) {
    *dst++ = '.';
  }
  /* Strip trailing slash, except if the entire path is "/". */
  if (dst > path + 1 && dst[-1] == '/') {
    dst--;
  }

  *dst = '\0';
  return dst - path;
}

const FileName* Process::get_absolute(const int dirfd, const char * const name, ssize_t length) {
  if (platform::path_is_absolute(name)) {
    return FileName::Get(name, length);
  } else {
    char on_stack_buf[2048], *buf;

    const FileName* dir;
    if (dirfd == AT_FDCWD) {
      dir = wd();
    } else {
      std::shared_ptr<FileFD> ffd = get_fd(dirfd);
      if (ffd) {
        dir = ffd->filename();
        if (!dir) {
          return nullptr;
        }
      } else {
        return nullptr;
      }
    }

    const size_t on_stack_buffer_size = sizeof(on_stack_buf);
    const ssize_t name_length = (length == -1) ? strlen(name) : length;
    const size_t total_buf_len = dir->length() + 1 + name_length + 1;
    if (on_stack_buffer_size < total_buf_len) {
      buf = reinterpret_cast<char *>(malloc(total_buf_len));
    } else {
      buf = reinterpret_cast<char *>(on_stack_buf);
    }
    memcpy(buf, dir->c_str(), dir->length());
    buf[dir->length()] = '/';
    memcpy(buf + dir->length() + 1, name, name_length);
    buf[total_buf_len - 1] = '\0';
    const size_t canonicalized_len = canonicalize_path(buf);
    const FileName* ret = FileName::Get(buf, canonicalized_len);
    if (on_stack_buffer_size < total_buf_len) {
      free(buf);
    }
    return ret;
  }
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
  if (actual.size() < expected.size()) {
    return false;
  }

  if (actual.size() == expected.size()) {
    /* If the length is the same, exact match is required. */
    return actual == expected;
  }

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

  /* For the remaining parameters exact match is needed. */
  for (unsigned int i = 1; i < expected.size(); i++) {
    if (actual[offset + i] != expected[i]) {
      return false;
    }
  }
  return true;
}

std::shared_ptr<std::vector<std::shared_ptr<FileFD>>>
Process::pop_expected_child_fds(const std::vector<std::string>& argv,
                                LaunchType *launch_type_p,
                                const bool failed) {
  std::shared_ptr<std::vector<std::shared_ptr<firebuild::FileFD>>> fds;
  if (expected_child_) {
    if (argv_matches_expectation(argv, expected_child_->argv())) {
      auto fds = expected_child_->fds();
      if (launch_type_p)
          *launch_type_p = expected_child_->launch_type();
      delete(expected_child_);
      expected_child_ = nullptr;
      return fds;
    } else {
      disable_shortcutting_bubble_up("Unexpected system/popen/posix_spawn child appeared: " +
                                     ::firebuild::pretty_print_array(argv) +
                                     " while waiting for: " +
                                     ::firebuild::to_string(*expected_child_));
    }
    delete(expected_child_);
    expected_child_ = nullptr;
  } else {
    disable_shortcutting_bubble_up("Unexpected system/popen/posix_spawn child " +
                                   std::string(failed ? "failed: " : "appeared: ") +
                                   firebuild::pretty_print_array(argv));
  }
  return std::make_shared<std::vector<std::shared_ptr<FileFD>>>();
}

bool Process::any_child_not_finalized() {
  if (exec_pending_ || pending_popen_child_) {
    return true;
  }
  if (exec_child() && exec_child()->state() != FB_PROC_FINALIZED) {
    /* The exec child is not yet finalized. We're not ready to finalize either. */
    return true;
  }

  for (auto child : children_) {
    if (child->state_ != FB_PROC_FINALIZED) {
      return true;
    }
  }
  return false;
}

/**
 * Finalize the current process.
 */
void Process::do_finalize() {
  /* Now we can ack the previous system()'s second message,
   * or a pending pclose() or wait*(). */
  if (on_finalized_ack_id_ != -1 && on_finalized_ack_fd_ != -1) {
    ack_msg(on_finalized_ack_fd_, on_finalized_ack_id_);
  }

  assert(state() == FB_PROC_TERMINATED);
  set_state(FB_PROC_FINALIZED);
}

/**
 * Finalize the current process if possible, and if successful then
 * bubble it up.
 */
void Process::maybe_finalize() {
  if (state() != FB_PROC_TERMINATED) {
    return;
  }
  if (any_child_not_finalized()) {
    /* A child is yet to be finalized. We're not ready to finalize. */
    return;
  }
  do_finalize();
  if (parent()) {
    parent()->maybe_finalize();
  }
}

void Process::finish() {
  if (FB_DEBUGGING(FB_DEBUG_PROC) && expected_child_) {
    FB_DEBUG(FB_DEBUG_PROC, "Expected system()/popen()/posix_spawn() children that did not appear"
                            " (e.g. posix_spawn() failed in the pre-exec or exec step):");
    FB_DEBUG(FB_DEBUG_PROC, "  " + to_string(*expected_child_));
  }
  set_state(FB_PROC_TERMINATED);
  maybe_finalize();
}

int64_t Process::sum_rusage_recurse() {
  if (exec_child_ != NULL) {
    aggr_time_ += exec_child_->sum_rusage_recurse();
  }
  for (auto& child : children_) {
    aggr_time_ += child->sum_rusage_recurse();
  }
  return aggr_time_;
}

void Process::export2js_recurse(const unsigned int level, FILE* stream,
                                unsigned int *nodeid) {
  if (exec_child() != NULL) {
    exec_child_->export2js_recurse(level + 1, stream, nodeid);
  }
  for (auto& child : children_) {
    child->export2js_recurse(level, stream, nodeid);
  }
}


Process::~Process() {
}


#if 0  /* unittests for canonicalize_path() */

/* Macro so that assert() reports useful line numbers. */
#define test(A, B) { \
  char *str = strdup(A); \
  canonicalize_path(str); \
  if (strcmp(str, B)) { \
    fprintf(stderr, "Error:  input: %s\n", A); \
    fprintf(stderr, "     expected: %s\n", B); \
    fprintf(stderr, "  got instead: %s\n", str); \
  } \
}

int main() {
  test("/", "/");
  test("/etc/hosts", "/etc/hosts");
  test("/usr/include/vte-2.91/vte/vteterminal.h", "/usr/include/vte-2.91/vte/vteterminal.h");
  test("/usr/bin/", "/usr/bin");
  test("/usr/bin/.", "/usr/bin");
  test("/usr/./bin", "/usr/bin");
  test("/./usr/bin", "/usr/bin");
  test("//", "/");
  test("", "");
  test(".", ".");
  test("/.", "/");
  test("./", ".");
  test("/./././", "/");
  test("./././.", ".");
  test("//foo//bar//", "/foo/bar");
  test("/././foo/././bar/././", "/foo/bar");
  test("///.//././/.///foo//.//bar//.", "/foo/bar");
  test("////foo/../bar", "/foo/../bar");
  test("/foo/bar/../../../../../", "/foo/bar/../../../../..");
  test("/.foo/.bar/..quux", "/.foo/.bar/..quux");
  test("foo", "foo");
  test("foo/bar", "foo/bar");
  test("././foo/./bar/./.", "foo/bar");
}

#endif  /* unittests for canonicalize_path() */


}  // namespace firebuild
