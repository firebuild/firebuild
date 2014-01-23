
#include "Process.h"

#include "File.h"
#include "FileDB.h"
#include "platform.h"

namespace firebuild {
  
static int fb_pid_counter;

Process::Process (const int pid, const int ppid, const process_type type, const std::string &wd)
    : type_(type), state_(FB_PROC_RUNNING), can_shortcut_(true),
      fb_pid_(fb_pid_counter++), pid_(pid), ppid_(ppid), exit_status_(-1), wd_(wd),
      fds_(), utime_m_(0), stime_m_(0), aggr_time_(0),
      children_(), exec_child_(NULL)
{
}

void Process::update_rusage (const long int utime_m, const long int stime_m)
{
  utime_m_ = utime_m;
  stime_m_ = stime_m;
}

void Process::exit_result (const int status, const long int utime_m, const long int stime_m)
{
  state_ = FB_PROC_FINISHED;
  exit_status_ = status;
  update_rusage(utime_m, stime_m);
}

void Process::sum_rusage(long int * const sum_utime_m, long int *const sum_stime_m)
{
  (*sum_utime_m) += utime_m_;
  (*sum_stime_m) += stime_m_;
  for (unsigned int i = 0; i < children_.size(); i++) {
    children_[i]->sum_rusage(sum_utime_m, sum_stime_m);
  }
}

int Process::open_file(const std::string ar_name, const int flags, const mode_t mode,
                       const int fd, const bool c, const int error)
{
  const bool created = (((flags & O_EXCL) && (fd != -1)) || c ||
                        ((fd == -1) && (error == ENOENT)));
  const std::string name = (platform::path_is_absolute(ar_name))?(ar_name):
      (wd_ + "/" + ar_name);

  FileUsage *fu;
  if (file_usages().count(name) > 0) {
    // the process already used this file
    fu = file_usages()[name];
  } else {
    fu = new FileUsage(flags, mode, created, false);
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

int Process::close_file(const int fd, const int error)
{
  if ((EIO == error) ||
      ((error == 0) && (fds_.size() <= static_cast<unsigned int>(fd)))) {
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

void Process::set_wd(const std::string &ar_d)
{
  const std::string d = (platform::path_is_absolute(ar_d))?(ar_d):
      (wd_ + "/" + ar_d);
  wd_ = d;

  add_wd(d);
}

Process::~Process()
{
  for (auto it = this->fds_.begin(); it != this->fds_.end(); ++it) {
    delete(*it);
  }

}

}
