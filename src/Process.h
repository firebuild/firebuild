
#ifndef FIREBUILD_PROCESS_H
#define FIREBUILD_PROCESS_H

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "FileFD.h"
#include "FileUsage.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{

typedef enum {FB_PROC_RUNNING, ///< process is running
              FB_PROC_EXECED,   ///< process finished running by exec()
              FB_PROC_FINISHED, ///< process exited cleanly
} process_state;

typedef enum {FB_PROC_EXEC_STARTED, ///< current process image is loaded by exec()
              FB_PROC_FORK_STARTED  ///< process is forked off from an other process
} process_type;

/**
 * Firebuild's model of a UNIX (TODO: Windows) process' period of life.
 * Generally it represents the period starting with a successful exec() or fork()
 * and finished by an other successful exec() or exit().
 *
 * Note the difference from UNIX's process concept. In Unix a process can call
 * exec() successfully several time preserving process id and some process
 * attributes, while replacing the process image. Those periods are handled as
 * different (but related) Processes in FireBuild.
 */
 class Process
{
public:
  const process_type type : 2;
  process_state state :2;
  bool can_shortcut:1;
  int fb_pid;       ///< internal FireBuild id for the process
  int pid;          ///< UNIX pid
  int ppid;         ///< UNIX ppid
  int exit_status;  ///< exit status, valid if state = FB_PROC_FINISHED
  std::set<std::string> libs; ///< DSO-s loaded by process, forked processes list new only
  std::unordered_map<std::string, FileUsage*> file_usages; ///< Usage per path
  std::vector<FileFD*> fds; ///< Active file descriptors
  long int utime_m; ///< user time in milliseconds as reported by getrusage()
  long int stime_m; ///< system time in milliseconds as reported by getrusage()
  long int aggr_time = 0; /**< Sum of user and system time in milliseconds for
                           * all forked and exec()-ed children */
  std::vector<Process*> children; ///< children of the process
  Process * exec_child = NULL;

  Process (int pid, int ppid, process_type type);
  virtual ~Process();
  bool operator == (Process const & p) const;
  void update_rusage (long int utime_m, long int stime_m);
  void sum_rusage(long int *sum_utime_m, long int *sum_stime_m);
  virtual void exit_result (int status, long int utime_m, long int stime_m);
  int open_file(const std::string name, const int flags, const mode_t mode,
                const int fd, const bool created = false, const int error = 0);
 private:
  DISALLOW_COPY_AND_ASSIGN(Process);
};

inline bool Process::operator == (Process const & p) const
{
  return (p.fb_pid == fb_pid);
}

}
#endif
