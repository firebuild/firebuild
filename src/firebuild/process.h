/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESS_H_
#define FIREBUILD_PROCESS_H_

#include <cassert>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "firebuild/file_fd.h"
#include "firebuild/execed_process_env.h"
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
  Process(int pid, int ppid, const FileName *wd,
          Process* parent, std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds);
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
  ExecedProcess* parent_exec_point() {return parent() ? parent()->exec_point() : NULL;}
  const ExecedProcess* parent_exec_point() const {return parent() ? parent()->exec_point() : NULL;}
  virtual bool exec_started() const {return false;}
  int state() const {return state_;}
  void set_state(process_state s) {state_ = s;}
  int fb_pid() {return fb_pid_;}
  int pid() const {return pid_;}
  int ppid() const {return ppid_;}
  int exit_status() const {return exit_status_;}
  void set_exit_status(const int e) {exit_status_ = e;}
  const FileName* wd() {return wd_;}
  void handle_set_wd(const char * const d);
  void handle_set_fwd(const int fd);
  int64_t utime_u() const {return utime_u_;}
  void set_utime_u(int64_t t) {utime_u_ = t;}
  int64_t stime_u() const {return stime_u_;}
  void set_stime_u(int64_t t) {stime_u_ = t;}
  int64_t aggr_time() const {return aggr_time_;}
  void set_aggr_time(int64_t t) {aggr_time_ = t;}
  void set_exec_pending(bool val) {exec_pending_ = val;}
  bool exec_pending() {return exec_pending_;}
  void set_posix_spawn_pending(bool val) {posix_spawn_pending_ = val;}
  bool posix_spawn_pending() {return posix_spawn_pending_;}
  void set_exec_child(Process *p) {exec_child_ = p;}
  Process* exec_child() const {return exec_child_;}
  std::vector<Process*>& fork_children() {return fork_children_;}
  const std::vector<Process*>& fork_children() const {return fork_children_;}
  void set_system_child(ExecedProcess *proc) {system_child_ = proc;}
  ExecedProcess *system_child() const {return system_child_;}
  void set_expected_child(ExecedProcessEnv *ec) {
    assert(!expected_child_);
    expected_child_ = ec;
  }
  ExecedProcess *pending_popen_child() const {return pending_popen_child_;}
  void set_pending_popen_child(ExecedProcess *proc) {
    assert(!pending_popen_child_ || !proc);
    pending_popen_child_ = proc;
  }
  int pending_popen_fd() const {return pending_popen_fd_;}
  void set_pending_popen_fd(int fd) {
    pending_popen_fd_ = fd;
  }
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>>
  pop_expected_child_fds(const std::vector<std::string>&,
                         LaunchType *launch_type_p,
                         const bool failed = false);
  bool has_expected_child () {return expected_child_ ? true : false;}
  virtual void do_finalize();
  virtual void maybe_finalize();
  void finish();
  virtual Process*  exec_proc() const = 0;
  void update_rusage(int64_t utime_u, int64_t stime_u);
  void sum_rusage(int64_t *sum_utime_u, int64_t *sum_stime_u);
  virtual void exit_result(int status, int64_t utime_u, int64_t stime_u);
  FileFD* get_fd(int fd) {
    assert(fds_);
    if (fd < 0 || static_cast<unsigned int>(fd) >= fds_->size()) {
      return nullptr;
    } else {
      return (*fds_)[fd].get();
    }
  }
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds() {return fds_;}
  void set_fds(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds) {fds_ = fds;}
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> pass_on_fds(bool execed = true);

  void AddPopenedProcess(int fd, ExecedProcess *proc) {
    fd2popen_child_[fd] = proc;
  }
  ExecedProcess *PopPopenedProcess(int fd) {
    assert(fd2popen_child_.count(fd) > 0);
    ExecedProcess *ret = fd2popen_child_[fd];
    fd2popen_child_.erase(fd);
    return ret;
  }

  /**
   * Return the resolved and canonicalized absolute pathname, based on the given dirfd directory
   * (possibly AT_FDCWD for the current directory). Return nullptr if the path is relative and
   * dirfd is invalid.
   */
  const FileName* get_absolute(const int dirfd, const char * const name, ssize_t length = -1);

  /**
   * Handle file opening in the monitored process
   * @param dirfd the dirfd of openat(), or AT_FDCWD
   * @param ar_name relative or absolute file name
   * @param flags flags of open()
   * @param fd the return value, or -1 if file was dlopen()ed successfully
   * @param error error code of open()
   * @param fd_conn fd to send ACK on when needed
   * @param ack_num ACK number to send or 0 if sending ACK is not needed
   */
  int handle_open(const int dirfd, const char * const ar_name, const int flags,
                  const int fd, const int error = 0, int fd_conn = -1, int ack_num = 0);

  /**
   * Handle file closure in the monitored process
   * @param fd file descriptor to close
   * @param error error code of close()
   */
  int handle_close(const int fd, const int error = 0);

  /**
   * Handle file closure in the monitored process when we don't know
   * if the operation succeeded or failed. Required for glibc's way of
   * ignoring an error from a close() that was registered by
   * posix_spawn_file_actions_addclose(). Also required as an internal
   * helper for opening to a particular fd, as done via
   * posix_spawn_file_actions_addopen().
   * @param fd file descriptor to close
   */
  int handle_force_close(const int fd);

  /**
   * Handle unlink in the monitored process
   * @param dirfd the dirfd of unlinkat(), or AT_FDCWD
   * @param name relative or absolute file name
   * @param flags flags passed to unlinkat()
   * @param error error code of unlink()
   */
  int handle_unlink(const int dirfd, const char * const name, const int flags, const int error = 0);

  /**
   * Handle rmdir in the monitored process
   * @param name relative or absolute file name
   * @param error error code of rmdir()
   */
  int handle_rmdir(const char * const name, const int error = 0);

  /**
   * Handle mkdir in the monitored process
   * @param dirfd the dirfd of mkdirat(), or AT_FDCWD
   * @param name relative or absolute file name
   * @param error error code of mkdir()
   */
  int handle_mkdir(const int dirfd, const char * const name, const int error = 0);

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
   * Handle rename()
   * @param olddirfd the olddirfd of renameat(), or AT_FDCWD
   * @param old_ar_name old relative or absolute file name
   * @param newdirfd the newdirfd of renameat(), or AT_FDCWD
   * @param new_ar_name new relative or absolute file name
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_rename(const int olddirfd, const char * const old_ar_name,
                    const int newdirfd, const char * const new_ar_name,
                    const int error = 0);

  /**
   * Handle symlink()
   * @param old_ar_name old relative or absolute file name
   * @param newdirfd the newdirfd of symlinkat(), or AT_FDCWD
   * @param new_ar_name new relative or absolute file name
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_symlink(const char * const old_ar_name,
                     const int newdirfd, const char * const new_ar_name,
                     const int error = 0);

  /**
   * Handle successfully clearing the cloexec bit, via a
   * posix_spawn_file_actions_adddup2() handler with oldfd==newfd.
   * @param fd file descriptor
   * @return 0 on success, -1 on failure
   */
  int handle_clear_cloexec(const int fd);

  /**
   * Handle fcntl() in the monitored process
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
   * Handle ioctl() in the monitored process
   * @param fd file descriptor
   * @param cmd ioctl's cmd parameter
   * @param ret ioctl's return value
   * @param error errno set by ioctl
   * @return 0 on success, -1 on failure
   */
  int handle_ioctl(const int fd, const int cmd,
                   const int ret, const int error = 0);


  /**
   * Handle read() in the monitored process
   * @param fd file descriptor
   */
  void handle_read(const int fd);

  /**
   * Handle write() in the monitored process
   * @param fd file descriptor
   */
  void handle_write(const int fd);

  /**
   * Fail to change to a working directory
   */
  virtual void handle_fail_wd(const char * const d) = 0;

  /**
   * Record visited working directory
   */
  virtual void add_wd(const FileName *d) = 0;

  /** Propagate exit status upward through exec()-ed processes */
  virtual void propagate_exit_status(const int status) = 0;

  /**
   * This particular process can't be short-cut because it performed calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  virtual void disable_shortcutting_only_this(const std::string& reason, const Process *p = NULL)
      = 0;

  /**
   * Process and parents (transitively) up to (excluding) "stop" can't be short-cut because
   * it performed calls preventing that.
   * @param stop Stop before this process
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  void disable_shortcutting_bubble_up_to_excl(const Process *stop, const std::string& reason,
                                              const Process *p = NULL) {
    if (this == stop) {
      return;
    }
    if (p == NULL) {
      p = this;
    }
    disable_shortcutting_only_this(reason, p);
    if (parent()) {
      parent()->disable_shortcutting_bubble_up_to_excl(stop, reason, p);
    }
  }

  /**
   * Process and parents (transitively) can't be short-cut because it performed
   * calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  void disable_shortcutting_bubble_up(const std::string& reason, const Process *p = NULL) {
    disable_shortcutting_bubble_up_to_excl(NULL, reason, p);
  }

  virtual int64_t sum_rusage_recurse();

  virtual void export2js_recurse(const unsigned int level, FILE* stream,
                                 unsigned int *nodeid);

  void set_on_finalized_ack(int id, int fd) {
    on_finalized_ack_id_ = id;
    on_finalized_ack_fd_ = fd;
  }

 private:
  Process *parent_;
  process_state state_ :2;
  int fb_pid_;       ///< internal FireBuild id for the process
  int pid_;          ///< UNIX pid
  int ppid_;         ///< UNIX ppid
  int exit_status_;  ///< exit status 0..255, or -1 if no exit() performed yet
  const FileName* wd_;  ///< Current working directory
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds_;  ///< Active file descriptors
  std::list<std::shared_ptr<FileFD>> closed_fds_;  ///< Closed file descriptors
  int64_t utime_u_;  ///< user time in microseconds as reported by getrusage()
  /// system time in microseconds as reported by getrusage()
  int64_t stime_u_;
  /** Sum of user and system time in microseconds for all forked and exec()-ed
      children */
  int64_t aggr_time_ = 0;
  std::vector<Process*> fork_children_;  ///< children of the process
  /// the latest system() child
  ExecedProcess *system_child_ {NULL};
  /// for popen()ed children: client fd -> process mapping
  std::unordered_map<int, ExecedProcess *> fd2popen_child_ {};
  /// if the popen()ed child has appeared, but the popen_parent messages hasn't:
  ExecedProcess *pending_popen_child_ {NULL};
  /// if the popen_parent message has arrived, but the popen()ed child hasn't:
  int pending_popen_fd_ {-1};
  /// commands of system(3), popen(3) and posix_spawn[p](3) that are expected to appear
  ExecedProcessEnv *expected_child_;
  /** Set upon an "execv" message, cleared upon "scproc_query" (i.e. new dynamically linked process
   *  successfully started up) or "execv_failed". Also set in the child upon a "posix_spawn_parent"
   *  in the typical case that the child hasn't appeared yet. Lets us detect statically linked
   *  processes: if a process quits while this flag is true then it was most likely statically
   *  linked (or failed to link, etc.). */
  bool exec_pending_ {false};
  /** Set upon "posix_spawn", cleared upon "posix_spawn_parent" or "posix_spawn_failed". Lets us
   *  detect the non-typical case when the posix_spawn'ed process appears (does an "scproc_query")
   *  sooner than the parent gets to "posix_spawn_parent". */
  bool posix_spawn_pending_ {false};
  Process * exec_child_;
  bool any_child_not_finalized();
  /** Add add ffd FileFD* to open fds */
  void add_filefd(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds,
                  const int fd, std::shared_ptr<FileFD> ffd);
  int on_finalized_ack_id_ = -1;
  int on_finalized_ack_fd_ = -1;
  DISALLOW_COPY_AND_ASSIGN(Process);
};

inline bool Process::operator == (Process const & p) const {
  return (p.fb_pid_ == fb_pid_);
}

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_H_
