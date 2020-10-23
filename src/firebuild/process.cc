/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <utility>

#include "firebuild/file.h"
#include "firebuild/platform.h"
#include "firebuild/execed_process.h"
#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"
#include "firebuild/utils.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const std::string &wd,
                 Process * parent,
                 std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>> fds)
    : parent_(parent), state_(FB_PROC_RUNNING), fb_pid_(fb_pid_counter++),
      pid_(pid), ppid_(ppid), exit_status_(-1), wd_(wd), fds_(fds),
      closed_fds_({}), utime_u_(0), stime_u_(0), aggr_time_(0), children_(),
      expected_child_(), exec_child_(NULL) {
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

std::shared_ptr<FileFD>
Process::add_filefd(std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>> fds,
                    int fd,
                    std::shared_ptr<FileFD> ffd) {
  if (fds->count(fd)) {
    firebuild::fb_error("Fd " + std::to_string(fd) + " is already tracked as being open.");
  }
  // the shared_ptr takes care of cleaning up the old fd if needed
  (*fds)[fd] = ffd;
  return ffd;
}

void Process::add_pipe(int fd1, std::shared_ptr<Pipe> pipe) {
  exec_point()->add_pipe(fd1, pipe);
}

void Process::forward_all_pipes() {
  for (const auto& it : *fds_) {
    auto file_fd = it.second;
    auto pipe = file_fd->pipe();
    if (pipe) {
      /* One round of forwarding should be enough, since the parent can't perform a
       * new write() after the exec(). */
      for (std::unordered_map<int, pipe_end *>::iterator it = pipe->fd1_ends.begin();
           it != pipe->fd1_ends.end();) {
        bool pipe_finished = false;
        auto fd1_end = it->second;
        auto ev = fd1_end->ev;
        assert(ev);
        int fd = event_get_fd(ev);
        auto res = pipe->forward(fd);
        switch (res) {
          case FB_PIPE_FD1_EOF: {
            it = pipe->fd1_ends.erase(it);
            close(fd);
            event_free(ev);
            delete(fd1_end);
            if (pipe->fd1_ends.size() > 0) {
              break;
            }
          }
            [[fallthrough]];
          case FB_PIPE_FD0_EOF: {
            pipe->finish();
            pipe_finished = true;
            break;
          }
          default:
            it++;
        }
        if (pipe_finished) {
          break;
        }
      }
    }
  }
}


std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>> Process::pass_on_fds(
    bool execed) {
  auto fds = std::make_shared<std::unordered_map<int, std::shared_ptr<FileFD>>>();
  for (const auto& it : *fds_) {
    if (!(execed && it.second->cloexec())) {
      add_filefd(fds, it.first, std::make_shared<FileFD>(*it.second));
    }
  }
  return fds;
}

int Process::handle_open(const std::string &ar_name, const int flags,
                         const int fd, const int error) {
  const std::string name = platform::path_is_absolute(ar_name) ? ar_name :
      wd() + "/" + ar_name;

  if (fd >= 0) {
    add_filefd(fds_, fd, std::make_shared<FileFD>(name, fd, flags, this));
  }

  if (!exec_point()->register_file_usage(name, name, FILE_ACTION_OPEN, flags, error)) {
    disable_shortcutting_bubble_up("Could not register the opening of " +
                                   pretty_print_string(name));
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
      fds_->erase(fd);
      return 0;
    } else if (((*fds_)[fd]->last_err() == EINTR) && (error == 0)) {
      // previous close got interrupted but the current one succeeded
      fds_->erase(fd);
      return 0;
    } else {
      // already closed, it may be an error
      // TODO(rbalint) debug
      fds_->erase(fd);
      return 0;
    }
  }
}

int Process::handle_unlink(const std::string &ar_name, const int error) {
  const std::string name = platform::path_is_absolute(ar_name) ? ar_name :
      wd() + "/" + ar_name;

  if (!error) {
    FileUsage fu(ISREG);
    fu.set_written(true);
    if (!exec_point()->register_file_usage(name, fu)) {
      disable_shortcutting_bubble_up("Could not register the unlinking of " +
                                     pretty_print_string(name));
      return -1;
    }
  }

  return 0;
}

int Process::handle_mkdir(const std::string &ar_name, const int error) {
  const std::string name = platform::path_is_absolute(ar_name) ? ar_name :
      wd() + "/" + ar_name;

  if (!exec_point()->register_file_usage(name, name, FILE_ACTION_MKDIR, 0, error)) {
    disable_shortcutting_bubble_up("Could not register the directory creation of " +
                                   pretty_print_string(name));
    return -1;
  }

  return 0;
}

int Process::handle_rmdir(const std::string &ar_name, const int error) {
  const std::string name = platform::path_is_absolute(ar_name) ? ar_name :
      wd() + "/" + ar_name;

  if (!error) {
    FileUsage fu(ISDIR);  // FIXME register that it's an _empty_ directory
    fu.set_written(true);
    if (!exec_point()->register_file_usage(name, fu)) {
      disable_shortcutting_bubble_up("Could not register the rmdir of " +
                                     pretty_print_string(name));
      return -1;
    }
  }

  return 0;
}

std::shared_ptr<Pipe> Process::handle_pipe(const int fd0, const int fd1, const int flags,
                                           const int error, int fd0_conn, int fd1_conn) {
  (void) flags; /* unused */
  if (error) {
    return std::shared_ptr<Pipe>(nullptr);
  }

  // validate fd-s
  if (get_fd(fd0)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting_bubble_up("Process created an fd (" + std::to_string(fd0) +
                                   ") which is known to be open, which means interception "
                                   "missed at least one close()");
    return std::shared_ptr<Pipe>(nullptr);
  }
  if (get_fd(fd1)) {
    // we already have this fd, probably missed a close()
    disable_shortcutting_bubble_up("Process created an fd (" + std::to_string(fd1) +
                                   ") which is known to be open, which means interception "
                                   "missed at least one close()");
    return std::shared_ptr<Pipe>(nullptr);
  }

  assert(fd0_conn != -1 && "connection to pipe's fd[0] is not valid");
  assert(fd1_conn != -1 && "connection to pipe's fd[1] is not valid");

  // TODO(rbalint) open cache files for fd1 and pass that
  auto cache_fds = std::vector<int>();
  auto pipe =  std::make_shared<Pipe>(fd0_conn, fd1_conn, std::move(cache_fds));
  add_filefd(fds_, fd0, std::make_shared<FileFD>(
      fd0, (flags & ~O_ACCMODE) | O_RDONLY, pipe, this));
  add_filefd(fds_, fd1, std::make_shared<FileFD>(
      fd1, (flags & ~O_ACCMODE) | O_WRONLY, pipe, this));

  return pipe;
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

int Process::handle_rename(const std::string &old_ar_name, const std::string &new_ar_name,
                           const int error) {
  if (error) {
    return 0;
  }

  const std::string old_name = platform::path_is_absolute(old_ar_name) ? old_ar_name :
      wd() + "/" + old_ar_name;
  const std::string new_name = platform::path_is_absolute(new_ar_name) ? new_ar_name :
      wd() + "/" + new_ar_name;

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

int Process::handle_symlink(const std::string &old_ar_name, const std::string &new_ar_name,
                            const int error) {
  if (!error) {
    disable_shortcutting_bubble_up("Process created a symlink (" +
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

void Process::set_wd(const std::string &ar_d) {
  const std::string d = platform::path_is_absolute(ar_d) ? ar_d :
      wd_ + "/" + ar_d;
  wd_ = d;

  add_wd(d);
}

std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>>
Process::pop_expected_child_fds(const std::vector<std::string>& argv,
                                LaunchType *launch_type_p,
                                const bool failed) {
  std::shared_ptr<std::unordered_map<int, std::shared_ptr<firebuild::FileFD>>> fds;
  if (expected_child_) {
    if (expected_child_->argv() == argv) {
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
  return std::make_shared<std::unordered_map<int, std::shared_ptr<FileFD>>>();
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

}  // namespace firebuild
