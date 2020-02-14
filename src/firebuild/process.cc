/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process.h"

#include <unistd.h>

#include "firebuild/file.h"
#include "firebuild/platform.h"
#include "firebuild/execed_process.h"
#include "firebuild/execed_process_parameters.h"
#include "firebuild/debug.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const std::string &wd,
                 Process * parent, bool execed)
    : parent_(parent), state_(FB_PROC_RUNNING), fb_pid_(fb_pid_counter++),
      pid_(pid), ppid_(ppid), exit_status_(-1), wd_(wd), fds_({NULL, NULL, NULL}),
      closed_fds_({}), utime_u_(0), stime_u_(0), aggr_time_(0), children_(),
      running_system_cmds_(), expected_children_(), exec_child_(NULL) {
  if (parent) {
    for (unsigned int i = 0; i < parent->fds_ .size(); i++) {
      if (parent->fds_.at(i) && ! (execed && parent->fds_[i]->cloexec())) {
        fds_.resize(i + 1);
        fds_[i] = parent->fds_.at(i)->inherit(this);
      }
    }
  } else {
    fds_[STDIN_FILENO]  = new FileFD(STDIN_FILENO, O_RDONLY, FD_ORIGIN_ROOT,
                                     NULL, this);
    fds_[STDOUT_FILENO] = new FileFD(STDOUT_FILENO, O_WRONLY, FD_ORIGIN_ROOT,
                                     NULL, this);
    fds_[STDERR_FILENO] = new FileFD(STDERR_FILENO, O_WRONLY, FD_ORIGIN_ROOT,
                                     NULL, this);
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

void Process::add_filefd(int fd, FileFD* ffd) {
  if (fds_.size() <= static_cast<unsigned int>(fd)) {
    fds_.resize(fd + 1, nullptr);
  }
  if (fds_[fd] != nullptr) {
    firebuild::fb_error("Fd " + std::to_string(fd) + " is already tracked as being open.");
    delete(fds_[fd]);
  }
  fds_[fd] = ffd;
}

int Process::handle_open(const std::string &ar_name, const int flags,
                         const int fd, const int error) {
  const std::string name = (platform::path_is_absolute(ar_name))?(ar_name):
      (wd() + "/" + ar_name);

  if (fd >= 0) {
    add_filefd(fd, new FileFD(name, fd, flags, this));
  }

  if (!exec_point()->register_file_usage(name, flags, error)) {
    disable_shortcutting("Could not register the opening of " + pretty_print_string(name));
    return -1;
  }

  return 0;
}

int Process::handle_close(const int fd, const int error) {
  if (EIO == error) {
    // IO prevents shortcutting
    disable_shortcutting("IO error closing fd " + fd);
    return -1;
  } else if (error == 0 && get_fd(fd) == nullptr) {
    // closing an unknown fd successfully prevents shortcutting
    disable_shortcutting("Process closed an unknown fd (" +
                         std::to_string(fd) + ") successfully, which means "
                         "interception missed at least one open()");
    return -1;
  } else if (EBADF == error) {
    // Process closed an fd unknown to it. Who cares?
    return 0;
  } else if (get_fd(fd) == nullptr) {
    // closing an unknown fd with not EBADF prevents shortcutting
    disable_shortcutting("Process closed an unknown fd (" +
                         std::to_string(fd) + ") successfully, which means "
                         "interception missed at least one open()");
    return -1;
  } else {
    if (fds_[fd]->open() == true) {
      fds_[fd]->set_open(false);
      if (fds_[fd]->last_err() != error) {
        fds_[fd]->set_last_err(error);
      }
      // remove from open fds
      closed_fds_.push_back(fds_[fd]);
      fds_[fd] = NULL;
      return 0;
    } else if ((fds_[fd]->last_err() == EINTR) && (error == 0)) {
      // previous close got interrupted but the current one succeeded
      return 0;
    } else {
      // already closed, it may be an error
      // TODO(rbalint) debug
      return 0;
    }
  }
}

int Process::handle_pipe(const int fd1, const int fd2, const int flags,
                         const int error) {
  switch (error) {
    case EFAULT:
    case EINVAL:
    case EMFILE:
    case ENFILE: {
      // pipe() failed
      return 0;
    }
    case 0:
    default:
      break;
  }

  // validate fd-s
  if (get_fd(fd1) != nullptr) {
    // we already have this fd, probably missed a close()
    disable_shortcutting("Process created an fd (" + std::to_string(fd1) +
                         ") which is known to be open, which means interception "
                         "missed at least one close()");
    return -1;
  }
  if (get_fd(fd2) != nullptr) {
    // we already have this fd, probably missed a close()
    disable_shortcutting("Process created an fd (" + std::to_string(fd2) +
                         ") which is known to be open, which means interception "
                         "missed at least one close()");
    return -1;
  }

  add_filefd(fd1, new FileFD(fd1, (flags & ~O_ACCMODE) | O_RDONLY, this));
  add_filefd(fd2, new FileFD(fd2, (flags & ~O_ACCMODE) | O_WRONLY, this));
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
  if (get_fd(oldfd) == nullptr) {
    // we already have this fd, probably missed a close()
    disable_shortcutting("Process created an fd (" + std::to_string(oldfd) +
                         ") which is known to be open, which means interception"
                         " missed at least one close()");
    return -1;
  }
  if (get_fd(newfd) != nullptr) {
    handle_close(newfd, 0);
  }

  add_filefd(newfd, new FileFD(newfd, ((fds_[oldfd]->flags() & ~O_CLOEXEC) | flags), FD_ORIGIN_DUP, fds_[oldfd], this));
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
        if (get_fd(fd) == nullptr) {
          disable_shortcutting("Process successfully fcntl'ed on fd (" + std::to_string(fd) +
                               ") which is known to be closed, which means interception"
                               " missed at least one open()");
          return -1;
        }
        fds_[fd]->set_cloexec(arg & FD_CLOEXEC);
      }
      return 0;
    default:
      disable_shortcutting("Process executed unsupported fcntl " + std::to_string(cmd));
      return 0;
  }
}

void Process::set_wd(const std::string &ar_d) {
  const std::string d = (platform::path_is_absolute(ar_d))?(ar_d):
      (wd_ + "/" + ar_d);
  wd_ = d;

  add_wd(d);
}

bool Process::remove_running_system_cmd(const std::string &cmd) {
  auto it = running_system_cmds_.find(cmd);
  if (it != running_system_cmds_.end()) {
    running_system_cmds_.erase(it);
    return true;
  }
  return false;
}

bool Process::remove_expected_child(const ExecedProcessParameters &ec) {
  auto item = std::find(expected_children_.begin(), expected_children_.end(), ec);
  if (item != expected_children_.end()) {
    expected_children_.erase(item);
    return true;
  } else {
    return false;
  }
}

/**
 * Finalize the current process.
 */
void Process::do_finalize() {
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
  if (exec_pending()) {
    /* A child is yet to appear. We're not ready to finalize. */
    return;
  }
  if (exec_child() && exec_child()->state() != FB_PROC_FINALIZED) {
    /* The exec child is not yet finalized. We're not ready to finalize either. */
    // TODO check for forked children in order to handle runaway processes
    return;
  }
  do_finalize();
  if (parent()) {
    parent()->maybe_finalize();
  }
}

void Process::finish() {
  if (FB_DEBUGGING(FB_DEBUG_PROC) && !expected_children_.empty()) {
    FB_DEBUG(FB_DEBUG_PROC, "Expected system()/popen()/posix_spawn() children that did not appear"
                            " (e.g. posix_spawn() failed in the pre-exec or exec step):");
    for (const auto &ec : expected_children_) {
      FB_DEBUG(FB_DEBUG_PROC, "  " + to_string(ec));
    }
  }
  set_state(FB_PROC_TERMINATED);
  maybe_finalize();
}

int64_t Process::sum_rusage_recurse() {
  if (exec_child_ != NULL) {
    aggr_time_ += exec_child_->sum_rusage_recurse();
  }
  for (auto child : children_) {
    aggr_time_ += child->sum_rusage_recurse();
  }
  return aggr_time_;
}

void Process::export2js_recurse(const unsigned int level, FILE* stream,
                                unsigned int *nodeid) {
  if (exec_child() != NULL) {
    exec_child_->export2js_recurse(level + 1, stream, nodeid);
  }
  for (auto child : children_) {
    child->export2js_recurse(level, stream, nodeid);
  }
}


Process::~Process() {
  for (auto fd : fds_) {
    delete(fd);
  }

  for (auto fd : closed_fds_) {
    delete(fd);
  }
}

}  // namespace firebuild
