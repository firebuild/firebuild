/* Copyright (c27) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process.h"

#include <unistd.h>

#include "firebuild/file.h"
#include "firebuild/platform.h"
#include "firebuild/execed_process.h"
#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const std::string &wd,
                 Process * parent, std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds)
    : parent_(parent), state_(FB_PROC_RUNNING), fb_pid_(fb_pid_counter++),
      pid_(pid), ppid_(ppid), exit_status_(-1), wd_(wd), fds_(fds),
      closed_fds_({}), utime_u_(0), stime_u_(0), aggr_time_(0), children_(),
      running_system_cmds_(), expected_child_(), exec_child_(NULL) {
  if (!fds_) {
    fds_ = std::make_shared<std::vector<std::shared_ptr<FileFD>>>();
    add_filefd(fds_, STDIN_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY, FD_ORIGIN_ROOT,
                                        std::shared_ptr<FileFD>(nullptr),
                                        this));
    add_filefd(fds_, STDOUT_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY, FD_ORIGIN_ROOT,
                                        std::shared_ptr<FileFD>(nullptr),
                                        this));
    add_filefd(fds_, STDERR_FILENO,
               std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY, FD_ORIGIN_ROOT,
                                        std::shared_ptr<FileFD>(nullptr),
                                        this));
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

int Process::handle_open(const std::string &ar_name, const int flags,
                         const int fd, const int error) {
  const std::string name = (platform::path_is_absolute(ar_name))?(ar_name):
      (wd() + "/" + ar_name);

  if (fd >= 0) {
    add_filefd(fds_, fd, std::make_shared<FileFD>(name, fd, flags, this));
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
    disable_shortcutting("IO error closing fd " + std::to_string(fd));
    return -1;
  } else if (error == 0 && !get_fd(fd)) {
    // closing an unknown fd successfully prevents shortcutting
    disable_shortcutting("Process closed an unknown fd (" +
                         std::to_string(fd) + ") successfully, which means "
                         "interception missed at least one open()");
    return -1;
  } else if (EBADF == error) {
    // Process closed an fd unknown to it. Who cares?
    return 0;
  } else if (!get_fd(fd)) {
    // closing an unknown fd with not EBADF prevents shortcutting
    disable_shortcutting("Process closed an unknown fd (" +
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
  if (get_fd(fd1)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting("Process created an fd (" + std::to_string(fd1) +
                         ") which is known to be open, which means interception "
                         "missed at least one close()");
    return -1;
  }
  if (get_fd(fd2)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting("Process created an fd (" + std::to_string(fd2) +
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
    disable_shortcutting("Process created an fd (" + std::to_string(oldfd) +
                         ") which is known to be open, which means interception"
                         " missed at least one close()");
    return -1;
  }
  if (get_fd(newfd)) {
    handle_close(newfd, 0);
  }

  add_filefd(fds_, newfd, std::make_shared<FileFD>(
      newfd, (((*fds_)[oldfd]->flags() & ~O_CLOEXEC) | flags), FD_ORIGIN_DUP,
      (*fds_)[oldfd], this));
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
          disable_shortcutting("Process successfully fcntl'ed on fd (" + std::to_string(fd) +
                               ") which is known to be closed, which means interception"
                               " missed at least one open()");
          return -1;
        }
        (*fds_)[fd]->set_cloexec(arg & FD_CLOEXEC);
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

std::shared_ptr<std::vector<std::shared_ptr<FileFD>>>
Process::pop_expected_child_fds(const std::vector<std::string>& argv, const bool failed) {
  std::shared_ptr<std::vector<std::shared_ptr<firebuild::FileFD>>> fds;
  if (expected_child_) {
    if (expected_child_->argv() == argv) {
      auto fds = expected_child_->fds();
      delete(expected_child_);
      expected_child_ = nullptr;
      return fds;
    } else {
      disable_shortcutting("Unexpected system/popen/posix_spawn child appeared: " +
                           ::firebuild::pretty_print_array(argv) +
                           "while waiting for: " + ::firebuild::to_string(*expected_child_));
    }
    delete(expected_child_);
    expected_child_ = nullptr;
  } else {
    disable_shortcutting("Unexpected system/popen/posix_spawn child " +
                         std::string(failed?"failed: ":"appeared: ") +
                         firebuild::pretty_print_array(argv));
  }
  return std::make_shared<std::vector<std::shared_ptr<FileFD>>>();
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
    // TODO(rbalint) check for forked children in order to handle runaway processes
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
}

}  // namespace firebuild
