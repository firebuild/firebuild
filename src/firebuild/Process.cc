/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/Process.h"

#include "firebuild/File.h"
#include "firebuild/FileDB.h"
#include "firebuild/platform.h"

namespace firebuild {

static int fb_pid_counter;

Process::Process(const int pid, const int ppid, const std::string &wd)
    : state_(FB_PROC_RUNNING), can_shortcut_(true),
      fb_pid_(fb_pid_counter++), pid_(pid), ppid_(ppid), exit_status_(-1),
      wd_(wd), fds_({NULL, NULL, NULL}), utime_m_(0), stime_m_(0),
      aggr_time_(0), children_(), exec_child_(NULL) {
  // TODO inherit fds properly
  fds_[0] = new FileFD(0, 0, (fd_origin)FD_ORIGIN_INHERITED);
  fds_[1] = new FileFD(1, 0, FD_ORIGIN_INHERITED);
  fds_[2] = new FileFD(2, 0, FD_ORIGIN_INHERITED);
}

void Process::update_rusage(const int64_t utime_m, const long int stime_m) {
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
          if (can_shortcut_) {
            can_shortcut_ = false;
          }
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

  if (fd != -1) {
    if (fds_.size() <= static_cast<unsigned int>(fd)) {
      fds_.resize(fd+1);
    }

    fds_[fd] = new FileFD(name, fd, flags);
  }
  return 0;
}

int Process::close_file(const int fd, const int error) {
  if ((EIO == error) ||
      ((error == 0) && (fds_.size() <= static_cast<unsigned int>(fd))) ||
      (NULL == fds_[fd])) {
    // IO error and closing an unknown fd succesfully prevents shortcutting
    // TODO debug
    this->can_shortcut_ = false;
    return -1;
  } else if (EBADF == error) {
    // Process closed an fd unknown to it. Who cares?
    return 0;
  } else {
    if (fds_[fd]->open() == true) {
      fds_[fd]->set_open(false);
      if (fds_[fd]->last_err() != error) {
        fds_[fd]->set_last_err(error);
      }
      return 0;
    } else if((fds_[fd]->last_err() == EINTR) && (error == 0)) {
      // previous close got interrupted but the current one succeeded
      return 0;
    } else {
      // already closed, it may be an error
      // TODO debug
      return 0;
    }
  }
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
}

}  // namespace firebuild
