/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESS_H_
#define FIREBUILD_PROCESS_H_

// Workaround for https://github.com/Tessil/hopscotch-map/issues/55
#ifndef __clang__
#pragma GCC optimize ("-fno-strict-aliasing")   // NOLINT(whitespace/parens)
#endif
#include <tsl/hopscotch_map.h>
#ifndef __clang__
#pragma GCC reset_options
#endif

#include <cassert>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <algorithm>

#include "firebuild/debug.h"
#include "firebuild/file_fd.h"
#include "firebuild/execed_process_env.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/pipe.h"

namespace firebuild {

class ExecedProcess;
class ForkedProcess;

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
   *
   * When a Process in our model enters the FB_PROC_TERMINATED state it checks if
   * it had any forked child that it did not wait for. If there were any,
   * the process can't be shortcut.
   *
   * Also a forked child stays in FB_PROC_TERMINATED state until its fork parent
   * waits for it or terminates.
   * TODO(rbalint) as a consequence an orphaned child not quitting on its own
   * hangs firebuild thus such children are not supported yet.
   */
  FB_PROC_TERMINATED,
  /**
   * The given process, and all its descendants have terminated. None of
   * the process's parameters can change anymore. Whatever the process
   * transitively performed is stored in the cache upon entering this
   * state.
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
 * different (but related) Processes in Firebuild.
 */
class Process {
 public:
  Process(int pid, int ppid, int exec_count, const FileName *wd, mode_t umask,
          Process* parent, std::vector<std::shared_ptr<FileFD>>* fds);
  virtual ~Process();
  bool operator == (Process const & p) const;
  void set_parent(Process *p) {parent_ = p;}
  Process* parent() {return parent_;}
  const Process* parent() const {return parent_;}
  /** The nearest ExecedProcess upwards in the tree, including "this".
   *  Guaranteed to be non-NULL. */
  virtual ExecedProcess* exec_point() = 0;
  virtual const ExecedProcess* exec_point() const = 0;
  virtual ForkedProcess* fork_point() = 0;
  virtual const ForkedProcess* fork_point() const = 0;
  Process* fork_parent();
  const Process* fork_parent() const;
  /** The nearest ExecedProcess upwards in the tree, excluding "this".
   *  Same as the parent's exec_point, with safe NULL handling. */
  ExecedProcess* parent_exec_point() {return parent() ? parent()->exec_point() : NULL;}
  const ExecedProcess* parent_exec_point() const {return parent() ? parent()->exec_point() : NULL;}
  virtual bool exec_started() const {return false;}
  /* This process has been wait()-ed for by the process that forked it. When the supervisor acts
   * as a subreaper it does not set the been_waited_for_ flag thus for those processes this function
   * never returns true. */
  virtual bool been_waited_for() const = 0;
  virtual void set_been_waited_for() = 0;
  /**
   * The process is either finalized or is terminated, but in case it is just terminated it has
   * only finalized, orphan or terminated children, recursively.
   * In other words all the descendants that are running are orphans or descendants of those
   * orphans and there is no terminated process that could be finalized.
   */
  bool finalized_or_terminated_and_has_orphan_and_finalized_children() const;
  /**
   * There is at least one child, that's not
   * finalized_or_terminated_and_has_orphan_and_finalized_children(). In other words there is
   * a running descendant that's not an orphan process or a terminated process that could be
   * finalized.
   */
  bool any_child_not_finalized_or_terminated_with_orphan() const;
  void terminate_top_orphans() const;
  int state() const {return state_;}
  void set_state(process_state s) {state_ = s;}
  int fb_pid() {return fb_pid_;}
  int pid() const {return pid_;}
  int ppid() const {return ppid_;}
  int exec_count() const {return exec_count_;}
  const FileName* wd() const {return wd_;}
  void handle_set_wd(const char * const d, const size_t d_len = -1);
  void handle_set_fwd(const int fd);
  mode_t umask() const {return umask_;}
  void set_exec_pending(bool val) {exec_pending_ = val;}
  bool exec_pending() const {return exec_pending_;}
  void set_posix_spawn_pending(bool val) {posix_spawn_pending_ = val;}
  bool posix_spawn_pending() {return posix_spawn_pending_;}
  void set_exec_child(ExecedProcess *p) {exec_child_ = p;}
  ExecedProcess* exec_child() const {return exec_child_;}
  Process* last_exec_descendant();
  const Process* last_exec_descendant() const;
  std::vector<ForkedProcess*>& fork_children() {return fork_children_;}
  const std::vector<ForkedProcess*>& fork_children() const {return fork_children_;}
  void set_system_child(ExecedProcess *proc) {system_child_ = proc;}
  ExecedProcess *system_child() const {return system_child_;}
  void set_expected_child(ExecedProcessEnv *ec) {
    assert_null(expected_child_);
    expected_child_ = ec;
  }
  std::vector<std::shared_ptr<FileFD>>*
  pop_expected_child_fds(const std::vector<std::string>&,
                         LaunchType *launch_type_p,
                         int *type_flags_p = nullptr,
                         const bool failed = false);
  bool has_expected_child () {return expected_child_ ? true : false;}
  void set_has_pending_popen(bool value) {has_pending_popen_ = value;}
  bool has_pending_popen() const {return has_pending_popen_;}
  virtual void do_finalize();
  virtual void set_on_finalized_ack(int id, int fd) = 0;
  virtual void maybe_finalize();
  void finish();
  virtual Process*  exec_proc() const = 0;
  void update_rusage(int64_t utime_u, int64_t stime_u);
  virtual void resource_usage(int64_t utime_u, int64_t stime_u);
  FileFD* get_fd(int fd) const {
    assert(fds_);
    if (fd < 0 || static_cast<unsigned int>(fd) >= fds_->size()) {
      return nullptr;
    } else {
      return (*fds_)[fd].get();
    }
  }
  std::shared_ptr<FileFD> get_shared_fd(int fd) {
    assert(fds_);
    if (fd < 0 || static_cast<unsigned int>(fd) >= fds_->size()) {
      return nullptr;
    } else {
      return (*fds_)[fd];
    }
  }
  void close_fds() {
    for (int i = fds_->size() - 1; i >= 0; i--) {
      FileFD* file_fd = get_fd(i);
      if (file_fd) {
        handle_close(file_fd);
      }
    }
  }
  std::vector<std::shared_ptr<FileFD>>* fds() {return fds_;}
  const std::vector<std::shared_ptr<FileFD>>* fds() const {return fds_;}
  void set_fds(std::vector<std::shared_ptr<FileFD>>* fds) {fds_ = fds;}
  /** Add add ffd FileFD* to open fds */
  static std::shared_ptr<FileFD> add_filefd(std::vector<std::shared_ptr<FileFD>>* fds,
                                            const int fd, std::shared_ptr<FileFD> ffd);
  std::shared_ptr<FileFD> add_filefd(const int fd, std::shared_ptr<FileFD> ffd);
  std::vector<std::shared_ptr<FileFD>>* pass_on_fds(const bool execed = true) const;
  void add_pipe(std::shared_ptr<Pipe> pipe);
  /** Drain all pipes's associated with open file descriptors of the process reading as much data
   *  as available on each fd1 end of each pipe */
  // TODO(rbalint) forward only fd0 pipe ends coming from this process
  void drain_all_pipes();
  void reset_file_fd_pipe_refs() {
    for (auto& file_fd : *fds_) {
      if (file_fd && file_fd->pipe()) {
        file_fd->pipe()->drain_fd1_end(file_fd.get());
        file_fd->set_pipe(nullptr);
      }
    }
  }

  void AddPopenedProcess(int fd, ExecedProcess *proc);

  ExecedProcess *PopPopenedProcess(int fd) {
    assert_cmp(fd2popen_child_.count(fd), >, 0);
    ExecedProcess *ret = fd2popen_child_[fd];
    fd2popen_child_.erase(fd);
    return ret;
  }

  /**
   * Return the resolved and canonicalized absolute pathname, based on the given dirfd directory
   * (possibly AT_FDCWD for the current directory). Return nullptr if the path is relative and
   * dirfd is invalid.
   */
  const FileName* get_absolute(const int dirfd, const char * const name, ssize_t length);

  /**
   * Handle preparation for file opening in the monitored process
   * @param dirfd the dirfd of openat(), or AT_FDCWD
   * @param ar_name relative or absolute file name
   * @param ar_len length of ar_name
   */
  int handle_pre_open(const int dirfd, const char * const ar_name, const size_t ar_len);

  /**
   * Handle file opening in the monitored process
   * @param dirfd the dirfd of openat(), or AT_FDCWD
   * @param ar_name relative or absolute file name
   * @param ar_len length of ar_name
   * @param flags flags of open()
   * @param mode mode (create permissions) of open()
   * @param fd the return value, or -1 if file was dlopen()ed successfully
   * @param error error code of open()
   * @param fd_conn fd to send ACK on when needed
   * @param ack_num ACK number to send or 0 if sending ACK is not needed
   * @param pre_open_sent interceptor already sent pre_open for this open
   */
  int handle_open(const int dirfd, const char * const ar_name, const size_t ar_len, const int flags,
                  const mode_t mode, const int fd, const int error = 0, int fd_conn = -1,
                  int ack_num = 0, const bool pre_open_sent = false);

  /**
   * Handle file opening in the monitored process
   * @param ar_name relative or absolute file name
   * @param ar_len length of ar_name
   * @param flags flags of open()
   * @param oldfd the fd of the stream before the operation
   * @param fd the fd of the stream after the operation
   * @param error error code of open()
   * @param fd_conn fd to send ACK on when needed
   * @param ack_num ACK number to send or 0 if sending ACK is not needed
   * @param pre_open_sent interceptor already sent pre_open for this open
   */
  int handle_freopen(const char * const ar_name, const size_t ar_len, const int flags,
                     const int oldfd, const int fd, const int error = 0,
                     int fd_conn = -1, int ack_num = 0, bool pre_open_sent = false);

  /**
   * Handle file closure in the monitored process
   * @param fd file descriptor to close
   * @param error error code of close()
   */
  int handle_close(const int fd, const int error = 0);

  /**
   * Handle file closure in the monitored process
   * @param file_fd FileFD* to close
   */
  void handle_close(FileFD * file_fd);

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
   * Handle closefrom() in the monitored process.
   */
  int handle_closefrom(const int lowfd);

  /**
   * Handle close_range() in the monitored process..
   */
  int handle_close_range(const unsigned int first, const unsigned int last,
                         const int flags, const int error = 0);

  /**
   * Handle unlink in the monitored process
   * @param dirfd the dirfd of unlinkat(), or AT_FDCWD
   * @param name relative or absolute file name
   * @param name_len length of name
   * @param flags flags passed to unlinkat()
   * @param error error code of unlink()
   */
  int handle_unlink(const int dirfd, const char * const name, const size_t name_len,
                    const int flags, const int error = 0);

  /**
   * Handle stat in the monitored process
   * @param fd fstat()'s fd, or fstatat()'s dirfd, or AT_FDCWD
   * @param name relative or absolute file name
   * @param name_len length of name
   * @param flags flags passed to fstatat() or AT_SYMLINK_NOFOLLOW in case of lstat()
   * @param st_mode mode as returned by a successful stat() call
   * @param st_size size as returned by a successful stat() call
   * @param error error code of stat() variant
   */
  int handle_fstatat(const int fd, const char * const name, const size_t name_len, const int flags,
                     const mode_t st_mode, const off_t st_size, const int error = 0);

  /**
   * Handle access, e[uid]access, faccessat in the monitored process
   */
  int handle_faccessat(const int dirfd, const char * const name, const size_t name_len,
                       const int mode, const int flags, const int error = 0);

  /**
   * Handle the chmod family in the monitored process
   * @param fd fchmod()'s fd, or fchmodat()'s dirfd, or AT_FDCWD
   * @param name relative or absolute file name
   * @param name_len length of name
   * @param mode the newly applied mode
   * @param flags flags passed to fchmodat() or AT_SYMLINK_NOFOLLOW in case of lchmod()
   * @param error error code of stat() variant
   */
  int handle_fchmodat(const int fd, const char * const name, const size_t name_len,
                      const mode_t mode, const int flags, const int error = 0);

  /**
   * Handle memfd_create in the monitored process
   */
  int handle_memfd_create(const int flags, const int fd);

  /**
   * Handle timerfd_create in the monitored process
   */
  int handle_timerfd_create(const int flags, int fd);

  /**
   * Handle epoll_create in the monitored process
   */
  int handle_epoll_create(const int flags, const int fd);

  /**
   * Handle eventfd in the monitored process
   */
  int handle_eventfd(const int flags, const int fd);

  /**
   * Handle signalfd in the monitored process
   * @param oldfd fd passed to signalfd() to be reused (if not -1)
   * @param flags flags passed to signalfd()
   * @param newfd new fd returned by signalfd()
   */
  int handle_signalfd(const int oldfd, const int flags, const int newfd);

  /**
   * Handle rmdir in the monitored process
   * @param name relative or absolute file name
   * @param name_len length of name
   * @param error error code of rmdir()
   */
  int handle_rmdir(const char * const name, const size_t name_len, const int error = 0);

  /**
   * Handle mkdir in the monitored process
   * @param dirfd the dirfd of mkdirat(), or AT_FDCWD
   * @param name relative or absolute file name
   * @param name_len length of name
   * @param error error code of mkdir()
   */
  int handle_mkdir(const int dirfd, const char * const name, const size_t name_len,
                   const int error = 0);

  /**
   * Handle pipe creation in the monitored process, steps 1 and 2 out of 3. See #656 for the design.
   * @param flags The flags passed to pipe2(), or 0 if pipe() was called
   * @param fd_conn fd to send the pipe_fds message to
   */
  void handle_pipe_request(const int flags, const int fd_conn);

  /**
   * Handle pipe creation in the monitored process, step 3 out of 3. See #656 for the design.
   * @param fd0 fd number of the reading end in the monitored process
   * @param fd1 fd number of the writing end in the monitored process
   */
  void handle_pipe_fds(const int fd0, const int fd1);

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
   * @param old_ar_len length of old_ar_name
   * @param newdirfd the newdirfd of renameat(), or AT_FDCWD
   * @param new_ar_name new relative or absolute file name
   * @param new_ar_len length of old_ar_name
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_rename(const int olddirfd, const char * const old_ar_name, const size_t old_ar_len,
                    const int newdirfd, const char * const new_ar_name, const size_t new_ar_len,
                    const int error = 0);

  /**
   * Handle symlink()
   * @param target relative or absolute target file name
   * @param newdirfd the newdirfd of symlinkat(), or AT_FDCWD
   * @param new_ar_name new relative or absolute file name
   * @param error error code
   * @return 0 on success, -1 on failure
   */
  int handle_symlink(const char * const target,
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
   * Handle first read() to an inherited fd in the monitored process
   * @param fd file descriptor
   */
  void handle_read_from_inherited(const int fd);

  /**
   * Handle first write() to an inherited fd in the monitored process
   * @param fd file descriptor
   */
  void handle_write_to_inherited(const int fd);

  /**
   * Fail to change to a working directory
   */
  virtual void handle_fail_wd(const char * const d) = 0;

  /**
   * Record visited working directory
   */
  virtual void add_wd(const FileName *d) = 0;

  /**
   * Handle umask() in the monitored process
   */
  void handle_umask(mode_t old_umask, mode_t new_umask);

  virtual void export2js_recurse(const unsigned int level, FILE* stream,
                                 unsigned int *nodeid);

  /* For debugging. */
  std::string pid_and_exec_count() const {return d(pid()) + "." + d(exec_count());}
  /* For debugging. */
  std::string state_string() const {
    switch (state_) {
      case FB_PROC_RUNNING:
        return "running";
      case FB_PROC_TERMINATED:
        return "terminated";
      case FB_PROC_FINALIZED:
        return "finalized";
      default:
        assert(0 && "unknown state");
        return "UNKNOWN";
    }
  }
  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  virtual std::string d_internal(const int level = 0) const;

 private:
  Process *parent_;
  process_state state_ :2;
  int fb_pid_;       ///< internal Firebuild id for the process
  int pid_;          ///< UNIX pid
  int ppid_;         ///< UNIX ppid
  /** For debugging: The "age" of a given PID, i.e. how many execve() hops happened to it.
   *  0 for a ForkedProcess, 1 for its first ExecedProcess child, 2 for the execed child of
   *  that one, etc. -1 temporarily while constructing a Process object. */
  int exec_count_;
  const FileName* wd_;  ///< Current working directory
  mode_t umask_;  ///< Current umask
  std::vector<std::shared_ptr<FileFD>>* fds_;  ///< Active file descriptors
  std::vector<ForkedProcess*> fork_children_;  ///< children of the process
  /** the latest system() child */
  ExecedProcess *system_child_ {NULL};
  /** Same as `proc_tree->Proc2PendingPopen(this) != nullptr`, redundantly repeated here
   *  for better performance. */
  bool has_pending_popen_ {false};
  /** for popen()ed children: client fd -> process mapping */
  tsl::hopscotch_map<int, ExecedProcess *> fd2popen_child_ {};
  /** commands of system(3), popen(3) and posix_spawn[p](3) that are expected to appear */
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
  ExecedProcess * exec_child_;
  const FileName* get_fd_filename(int fd) const;
  bool any_child_not_finalized();
  DISALLOW_COPY_AND_ASSIGN(Process);
};

inline bool Process::operator == (Process const & p) const {
  return (p.fb_pid_ == fb_pid_);
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Process& p, const int level = 0);
std::string d(const Process *p, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_H_
