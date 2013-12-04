
#include "Process.h"

#include "File.h"
#include "FileDB.h"

using namespace std;
namespace firebuild {
  
static int fb_pid_counter;

Process::Process (int pid, int ppid, process_type type)
    : type(type), can_shortcut(true), pid(pid), ppid(ppid)
{
  fb_pid = fb_pid_counter++;
  state = FB_PROC_RUNNING;
}

void Process::update_rusage (long int utime_m, long int stime_m)
{
  this->utime_m = utime_m;
  this->stime_m = stime_m;
}

void Process::exit_result (int status, long int utime_m, long int stime_m)
{
  state = FB_PROC_FINISHED;
  exit_status = status;
  update_rusage(utime_m, stime_m);
}

void Process::sum_rusage(long int *sum_utime_m, long int *sum_stime_m)
{
  (*sum_utime_m) += utime_m;
  (*sum_stime_m) += stime_m;
  for (unsigned int i = 0; i < children.size(); i++) {
    children[i]->sum_rusage(sum_utime_m, sum_stime_m);
  }
}
int Process::open_file(const string name, const int flags, const mode_t mode,
                       const int fd, const bool c, const int error)
{
  const bool created = (((flags & O_EXCL) && (fd != -1)) || c ||
                        ((fd == -1) && (error == ENOENT)));
  FileUsage *fu;
  File *f;
  
  if (file_usages.count(name) > 0) {
    // the process already used this file
    fu = file_usages[name];
  } else {
    fu = new FileUsage(flags, mode, created, false);
    file_usages[name] = fu;
  }

  // record unhandled errors
  if (fd == -1) {
    switch (error) {
      case ENOENT:
        break;
      default:
        if (0 == fu->unknown_err) {
          fu->unknown_err = error;
          if (this->can_shortcut) {
            this->can_shortcut = false;
          }
        }
    }
  }

  {
    FileDB *fdb = FileDB::getInstance();
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
    fu->initial_hash = f->hash;
  }

  if (fd != -1) {
    if (fds.size() <= static_cast<unsigned int>(fd)) {
      fds.resize(fd+1);
    }

    fds[fd] = FileFD(name, fd, flags);
  }
  return 0;
}

Process::~Process()
{
  for (auto it = this->file_usages.begin(); it != this->file_usages.end(); ++it) {
    delete(it->second);
  }

}

}
