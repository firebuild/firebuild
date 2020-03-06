/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESS_H_
#define FIREBUILD_PROCESS_H_

#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "firebuild/file_fd.h"
#include "firebuild/execed_process_parameters.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess;

typedef enum {
  /**
   * Process is running.
   */
  FB_PROC_RUNNING,
  /**
   * Process successfully performed an exit() or alike, exec() or alike,
   * or crashed on signal.
   *
   * In case of exec() it lives on with the same Unix PID, but that's a
   * different Process in our model. Either exec_pending_ is set (the
   * execed process haven't appeared yet) or exec_child_ is set
   * (pointing to the execed process).
   *
   * In case of exit() or crash it might still be present as a Unix
   * zombie process, we don't care about that. Neither exec_pending_ nor
   * exec_child_ are set.
   */
  FB_PROC_TERMINATED,
  /**
   * The given process, and all its descendants have terminated. None of
   * the process's parameters can change anymore. Whatever the process
   * transitively performed is stored in the cache upon entering this
   * state.
   *
   * We don't support runaway forked processes yet. So when the last
   * process in an exec chain terminates, all processes in the exec
   * chain enter this state.
   *
   * Once support for runaway processes is added, forked descendants
   * will also have to be waited for before entering this state.
   */
  FB_PROC_FINALIZED,
} process_state;

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
class Process {
 public:
  Process(int pid, int ppid, const std::string &wd,
          Process* parent, bool execed = false);
  virtual ~Process();
  bool operator == (Process const & p) const;
  void set_parent(Process *p) {parent_ = p;}
  Process* parent() {return parent_;}
  const Process* parent() const {return parent_;}
  /** The nearest ExecedProcess upwards in the tree, including "this".
   *  Guaranteed to be non-NULL. */
  virtual ExecedProcess* exec_point() = 0;
  virtual const ExecedProcess* exec_point() const = 0;
  /** The nearest ExecedProcess upwards in the tree, excluding "this".
   *  Same as the parent's exec_point, with safe NULL handling. */
  ExecedProcess* parent_exec_point() {return parent()?parent()->exec_point():NULL;}
  const ExecedProcess* parent_exec_point() const {return parent()?parent()->exec_point():NULL;}
  virtual bool exec_started() const {return false;}
  int state() const {return state_;}
  void set_state(process_state s) {state_ = s;}
  int fb_pid() {return fb_pid_;}
  int pid() const {return pid_;}
  int ppid() const {return ppid_;}
  int exit_status() const {return exit_status_;}
  void set_exit_status(const int e) {exit_status_ = e;}
  std::string& wd() {return wd_;}
  void set_wd(const std::string &d);
  int64_t utime_u() const {return utime_u_;}
  void set_utime_u(int64_t t) {utime_u_ = t;}
  int64_t stime_u() const {return stime_u_;}
  void set_stime_u(int64_t t) {stime_u_ = t;}
  int64_t aggr_time() const {return aggr_time_;}
  void set_aggr_time(int64_t t) {aggr_time_ = t;}
  void set_exec_pending(bool val) {exec_pending_ = val;}
  bool exec_pending() {return exec_pending_;}
  void set_exec_child(Process *p) {exec_child_ = p;}
  Process* exec_child() const {return exec_child_;}
  std::vector<Process*>& children() {return children_;}
  const std::vector<Process*>& children() const {return children_;}
  void add_running_system_cmd(const std::string &cmd) {running_system_cmds_.insert(cmd);}
  bool remove_running_system_cmd(const std::string &cmd);
  bool has_running_system_cmd(const std::string &cmd) {
    return (running_system_cmds_.find(cmd) != running_system_cmds_.end());}
  void add_expected_child(const ExecedProcessParameters &ec) {expected_children_.push_back(ec);}
  bool remove_expected_child(const ExecedProcessParameters &ec);
  virtual void do_finalize();
  virtual void maybe_finalize();
  void finish();
  virtual Process*  exec_proc() const = 0;
  void update_rusage(int64_t utime_u, int64_t stime_u);
  void sum_rusage(int64_t *sum_utime_u, int64_t *sum_stime_u);
  virtual void exit_result(int status, int64_t utime_u, int64_t stime_u);
  std::shared_ptr<FileFD> get_fd(int fd) {
    try {
      return fds_.at(fd);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }

  /**
   * Handle file opening in the monitored process
   * @param name relative or absolute file name
   * @param flags flags of open()
   * @param fd the return value
   * @param error error code of open()
   */
  int handle_open(const std::string &name, const int flags,
                  const int fd, const int error = 0);

  /**
   * Handle file closure in the monitored process
   * @param fd file descriptor to close
   * @param error error code of close()
   */
  int handle_close(const int fd, const int error = 0);
  /**
   * Handle pipe() in the monitored process
   * @param fd1 file descriptor to read
   * @param fd2 file descriptor to write
   * @param flags flags passed in pipe2()
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_pipe(const int fd1, const int fd2, const int flags,
                  const int error = 0);
  /**
   * Handle dup(), dup2() or dup3() in the monitored process
   * @param oldfd old fd
   * @param newfd new fd
   * @param flags extra flags for new fd passed to dup3()
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_dup3(const int oldfd, const int newfd, const int flags,
                  const int error = 0);
  /**
   * Handle fcntl() in the monitored process
   *
   * @param fd file descriptor
   * @param cmd fcntl's cmd parameter
   * @param arg fcntl's arg parameter
   * @param ret fcntl's return value
   * @param error errno set by fcntl
   * @return 0 on success, -1 on failure
   */
  int handle_fcntl(const int fd, const int cmd, const int arg,
                   const int ret, const int error = 0);

  /**
   * Fail to change to a working directory
   */
  virtual void fail_wd(const std::string &d) = 0;

  /**
   * Record visited working directory
   */
  virtual void add_wd(const std::string &d) = 0;

  /** Propagate exit status upward through exec()-ed processes */
  virtual void propagate_exit_status(const int status) = 0;

  /**
   * Process and parents (transitively) can't be short-cut because it performed
   * calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  virtual void disable_shortcutting(const std::string& reason, const Process *p = NULL) = 0;

  virtual int64_t sum_rusage_recurse();

  virtual void export2js_recurse(const unsigned int level, FILE* stream,
                                 unsigned int *nodeid);

 private:
  Process *parent_;
  process_state state_ :2;
  int fb_pid_;       ///< internal FireBuild id for the process
  int pid_;          ///< UNIX pid
  int ppid_;         ///< UNIX ppid
  int exit_status_;  ///< exit status 0..255, or -1 if no exit() performed yet
  std::string wd_;  ///< Current working directory
  std::vector<std::shared_ptr<FileFD>> fds_;  ///< Active file descriptors
  std::list<std::shared_ptr<FileFD>> closed_fds_;  ///< Closed file descriptors
  int64_t utime_u_;  ///< user time in microseconds as reported by getrusage()
  /// system time in microseconds as reported by getrusage()
  int64_t stime_u_;
  /** Sum of user and system time in microseconds for all forked and exec()-ed
      children */
  int64_t aggr_time_ = 0;
  std::vector<Process*> children_;  ///< children of the process
  /// commands of system(3) calls which did not finish yet
  std::multiset<std::string> running_system_cmds_;
  /// commands of system(3), popen(3) and posix_spawn[p](3) that are expected to appear
  std::vector<ExecedProcessParameters> expected_children_;
  bool exec_pending_ {false};
  Process * exec_child_;
  /** Add add ffd FileFD* to open fds */
  void add_filefd(const int fd, std::shared_ptr<FileFD> ffd);
  DISALLOW_COPY_AND_ASSIGN(Process);
};

inline bool Process::operator == (Process const & p) const {
  return (p.fb_pid_ == fb_pid_);
}

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_H_
