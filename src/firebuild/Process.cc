/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/Process.h"

#include <unistd.h>

#include "firebuild/File.h"
#include "firebuild/FileDB.h"
#include "firebuild/platform.h"
#include "firebuild/Debug.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const std::string &wd,
                 Process * parent)
    : state_(FB_PROC_RUNNING), fb_pid_(fb_pid_counter++), pid_(pid),
      ppid_(ppid), exit_status_(-1), wd_(wd), fds_({NULL, NULL, NULL}),
      closed_fds_({}), utime_m_(0), stime_m_(0), aggr_time_(0), children_(),
      exec_child_(NULL) {
  if (parent) {
    for (unsigned int i = 0; i < parent->fds_ .size(); i++) {
      if (parent->fds_.at(i)) {
        fds_.reserve(i + 1);
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

void Process::update_rusage(const int64_t utime_m, const int64_t stime_m) {
  utime_m_ = utime_m;
  stime_m_ = stime_m;
}

void Process::exit_result(const int status, const int64_t utime_m,
                          const int64_t stime_m) {
  state_ = FB_PROC_FINISHED;
  exit_status_ = status;
  update_rusage(utime_m, stime_m);
}

void Process::sum_rusage(int64_t * const sum_utime_m,
                         int64_t *const sum_stime_m) {
  (*sum_utime_m) += utime_m_;
  (*sum_stime_m) += stime_m_;
  for (unsigned int i = 0; i < children_.size(); i++) {
    children_[i]->sum_rusage(sum_utime_m, sum_stime_m);
  }
}

void Process::add_filefd(int fd, FileFD* ffd) {
  if (fds_.size() <= static_cast<unsigned int>(fd)) {
    auto size_orig = fds_.size();
    fds_.reserve(fd + 1);
    // fill new elements with default value
    for (auto i = size_orig; i < static_cast<unsigned int>(fd); i++) {
      fds_.push_back(NULL);
    }
  }

  fds_[fd] = ffd;
}

int Process::open_file(const std::string &ar_name, const int flags,
                       const mode_t mode, const int fd, const bool c,
                       const int error) {
  const bool created = ((flags & O_EXCL) && (fd != -1)) || c;
  const bool open_failed = (fd == -1);
  const std::string name = (platform::path_is_absolute(ar_name))?(ar_name):
      (wd_ + "/" + ar_name);

  FileUsage *fu;
  if (file_usages().count(name) > 0) {
    // the process already used this file
    fu = file_usages()[name];
  } else {
    fu = new FileUsage(flags, mode, created, false, open_failed, error);
    file_usages()[name] = fu;
  }

  // record unhandled errors
  if (fd == -1) {
    switch (error) {
      case ENOENT:
        break;
      default:
        if (0 == fu->unknown_err()) {
          fu->set_unknown_err(error);
          disable_shortcutting();
        }
    }
  }

  File *f;
  {
    auto *fdb = FileDB::getInstance();
    if (fdb->count(name) > 0) {
      // the build process already used this file
      f = (*fdb)[name];
    } else {
      f = new File(name);
      (*fdb)[name] = f;
    }
  }

  f->update();
  if (!created) {
    fu->set_initial_hash(f->hash());
  }

  if (fd >= 0) {
    add_filefd(fd, new FileFD(name, fd, flags, this));
  }
  return 0;
}

int Process::close_file(const int fd, const int error) {
  if ((EIO == error) ||
      ((error == 0) && (fds_.size() <= static_cast<unsigned int>(fd)))) {
    // IO error and closing an unknown fd succesfully prevents shortcutting
    // TODO(rbalint) debug
    disable_shortcutting();
    return -1;
  } else if (EBADF == error) {
    // Process closed an fd unknown to it. Who cares?
    return 0;
  } else if ((fds_.size() <= static_cast<unsigned int>(fd)) ||
             (NULL == fds_[fd])) {
    disable_shortcutting();
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

int Process::create_pipe(const int fd1, const int fd2, const int flags,
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
  if (((fds_.size() > static_cast<unsigned int>(fd1)) && (NULL != fds_[fd1])) ||
      ((fds_.size() > static_cast<unsigned int>(fd2)) && (NULL != fds_[fd2]))) {
    // we already have this fd, probably missed a close()
    disable_shortcutting();
    FB_DEBUG(3, "pipe() in intercepted process created a file descriptor"
             " already open. FireBuild probably missed a close().");
    return -1;
  }

  add_filefd(fd1, new FileFD(fd1, flags | O_RDONLY, this));
add_filefd(fd2, new FileFD(fd2, flags | O_WRONLY, this));
  return 0;
}

int Process::dup3(const int oldfd, const int newfd, const int flags,
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
  if ((fds_.size() <= static_cast<unsigned int>(oldfd)) || (NULL == fds_[oldfd])) {
    // we already have this fd, probably missed a close()
    disable_shortcutting();
    FB_DEBUG(3, "pipe() in intercepted process created a file descriptor"
             " already open. FireBuild probably missed a close().");
    return -1;
  }
  if ((fds_.size() > static_cast<unsigned int>(newfd)) && (NULL != fds_[newfd])) {
    close_file(newfd, 0);
  }

  add_filefd(newfd, new FileFD(newfd, ((fds_[oldfd]->flags() & ~O_CLOEXEC) | flags), FD_ORIGIN_DUP, fds_[oldfd], this));
  return 0;
}

void Process::set_wd(const std::string &ar_d) {
  const std::string d = (platform::path_is_absolute(ar_d))?(ar_d):
      (wd_ + "/" + ar_d);
  wd_ = d;

  add_wd(d);
}

int64_t Process::sum_rusage_recurse() {
  if (exec_child_ != NULL) {
    aggr_time_ += exec_child_->sum_rusage_recurse();
  }
  for (unsigned int i = 0; i < children_.size(); i++) {
    aggr_time_ += children_[i]->sum_rusage_recurse();
  }
  return aggr_time_;
}

void Process::export2js_recurse(const unsigned int level, FILE* stream,
                                unsigned int *nodeid) {
  if (exec_child() != NULL) {
    exec_child_->export2js_recurse(level + 1, stream, nodeid);
  }
  for (unsigned int i = 0; i < children().size(); i++) {
    children_[i]->export2js_recurse(level, stream, nodeid);
  }
}


Process::~Process() {
  for (auto it = this->fds_.begin(); it != this->fds_.end(); ++it) {
    delete(*it);
  }

  for (auto it = closed_fds_.begin(); it != closed_fds_.end(); ++it) {
    delete(*it);
  }
}

}  // namespace firebuild
