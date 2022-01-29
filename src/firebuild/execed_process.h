/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECED_PROCESS_H_
#define FIREBUILD_EXECED_PROCESS_H_

#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/file_usage.h"
#include "firebuild/pipe.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"

namespace firebuild {

class ExecedProcessCacher;

/**
 * Represents one outgoing pipe that this process inherited.
 *
 * The structure always refers to how things were when the process started,
 * it isn't modified later as the process does various things with its file descriptors.
 *
 * Accordingly, it does not hold a pointer to the Pipe object, since that one might go away
 * while we still need to keep this structure.
 *
 * A pipe might have multiple file descriptors, as per dup() and friends.
 * They are stored in ascending order. There's at least one fd.
 */
typedef struct inherited_outgoing_pipe_ {
  /* The client-side file descriptor numbers, sorted */
  std::vector<int> fds {};
  /* The recorder of the traffic, as seen from this exec point */
  std::shared_ptr<PipeRecorder> recorder {};
} inherited_outgoing_pipe_t;

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const FileName *initial_wd,
                         const FileName *executable, const FileName *executed_path,
                         const std::vector<std::string>& args,
                         const std::vector<std::string>& env_vars,
                         const std::vector<const FileName*>& libs,
                         Process * parent,
                         std::vector<std::shared_ptr<FileFD>>* fds);
  virtual ~ExecedProcess();
  virtual bool exec_started() const {return true;}
  ExecedProcess* exec_point() {return this;}
  const ExecedProcess* exec_point() const {return this;}
  const Process* fork_parent() const {
    /* Direct parent can't be a fork parent because this is an execed process. */
    const Process* fork_parent_candidate = parent() ? parent()->parent() : nullptr;
    /* The fork_parent() function is expected to be called very rarely itself and this loop is
     * entered only when the process with the same pid exec()-ed more than once.
     * this could be implemented symmetrically with exec_point(), but the exec_point() function
     * is called very often thus it made more sense to make it quicker at the expense of an having
     * and the extra ForkedProcess::exec_point_ private member. */
    while (fork_parent_candidate && fork_parent_candidate->pid() == pid())  {
      fork_parent_candidate = fork_parent_candidate->parent();
    }
    return fork_parent_candidate;
  }
  void add_utime_u(int64_t t) {utime_u_ += t;}
  int64_t utime_u() const {return utime_u_;}
  void add_stime_u(int64_t t) {stime_u_ += t;}
  int64_t stime_u() const {return stime_u_;}
  int64_t cpu_time_u() const {return utime_u_ + stime_u_;}
  void add_children_cpu_time_u(const int64_t t) {children_cpu_time_u_ += t;}
  int64_t aggr_cpu_time_u() const {return cpu_time_u() + children_cpu_time_u_;}
  const FileName* initial_wd() const {return initial_wd_;}
  const tsl::hopscotch_set<const FileName*>& wds() const {return wds_;}
  const tsl::hopscotch_set<const FileName*>& wds() {return wds_;}
  const tsl::hopscotch_set<const FileName*>& failed_wds() const {return wds_;}
  tsl::hopscotch_set<const FileName*>& failed_wds() {return failed_wds_;}
  const std::vector<std::string>& args() const {return args_;}
  std::vector<std::string>& args() {return args_;}
  const std::vector<std::string>& env_vars() const {return env_vars_;}
  std::vector<std::string>& env_vars() {return env_vars_;}
  const FileName* executable() const {return executable_;}
  const FileName* executed_path() const {return executed_path_;}
  std::vector<const FileName*>& libs() {return libs_;}
  const std::vector<const FileName*>& libs() const {return libs_;}
  tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages() {return file_usages_;}
  const tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages() const {
    return file_usages_;
  }
  void set_cacher(ExecedProcessCacher *cacher) {cacher_ = cacher;}
  void do_finalize();
  Process* exec_proc() const {return const_cast<ExecedProcess*>(this);}
  void exit_result(const int status, const int64_t utime_u,
                   const int64_t stime_u);

  void initialize();
  void propagate_file_usage(const FileName *name,
                            const FileUsage* fu_change);
  bool register_file_usage(const FileName *name, const FileName *actual_file,
                           FileAction action, int flags, int error);
  bool register_file_usage(const FileName *name, const FileUsage* fu_change);
  bool register_parent_directory(const FileName *name);
  void add_pipe(std::shared_ptr<Pipe> pipe) {created_pipes_.insert(pipe);}
  std::vector<inherited_outgoing_pipe_t>& inherited_outgoing_pipes()
      {return inherited_outgoing_pipes_;}
  const std::vector<inherited_outgoing_pipe_t>& inherited_outgoing_pipes() const
      {return inherited_outgoing_pipes_;}
  void set_inherited_outgoing_pipes(std::vector<inherited_outgoing_pipe_t> inherited_outgoing_pipes)
      {inherited_outgoing_pipes_ = inherited_outgoing_pipes;}

  /**
   * Fail to change to a working directory
   */
  void handle_fail_wd(const char * const d) {
    failed_wds_.insert(FileName::Get(d));
  }
  /**
   * Record visited working directory
   */
  void add_wd(const FileName *d) {
    wds_.insert(d);
  }

  /** Returns if the process can be short-cut */
  bool can_shortcut() const {return can_shortcut_;}

  bool shortcut();

  virtual void propagate_exit_status(const int status);
  /**
   * This particular process can't be short-cut because it performed calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  virtual void disable_shortcutting_only_this(const char* reason,
                                              const ExecedProcess *p = NULL);
  /**
   * Process and parents (transitively) up to (excluding) "stop" can't be short-cut because
   * it performed calls preventing that.
   * @param stop Stop before this process
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   * @param shortcutable_ancestor this ancestor will be the nearest shortcutable ancestor
   *        for all visited execed processes after this call
   *        (when shortcutable_ancestor_is_set is true)
   * @param shortcutable_ancestor_is_set the shortcutable_ancestor is computed
   */
  void disable_shortcutting_bubble_up_to_excl(ExecedProcess *stop, const char* reason,
                                              const ExecedProcess *p = NULL,
                                              ExecedProcess *shortcutable_ancestor = nullptr,
                                              bool shortcutable_ancestor_is_set = false);
  void disable_shortcutting_bubble_up_to_excl(ExecedProcess *stop, const char* reason, int fd,
                                              const ExecedProcess *p = NULL,
                                              ExecedProcess *shortcutable_ancestor = nullptr,
                                              bool shortcutable_ancestor_is_set = false);
  /**
   * Process and parents (transitively) can't be short-cut because it performed
   * calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  void disable_shortcutting_bubble_up(const char* reason, const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const int fd,
                                      const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const FileName& file,
                                      const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const std::string& str,
                                      const ExecedProcess *p = NULL);

  bool was_shortcut() const {return was_shortcut_;}
  void set_was_shortcut(bool value) {was_shortcut_ = value;}

  void export2js(const unsigned int level, FILE* stream,
                 unsigned int * nodeid);
  void export2js_recurse(const unsigned int level, FILE* stream,
                         unsigned int *nodeid);

  std::string args_to_short_string() const;
  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  virtual std::string d_internal(const int level = 0) const;

 private:
  bool can_shortcut_:1;
  bool was_shortcut_:1;
  /** If points to this (self), the process can be shortcut.
      Otherwise the process itself is not shortcutable, but the ancestor is, if the ancestor's
      maybe_shortcutable_ancestor points at itself, etc. */
  ExecedProcess * maybe_shortcutable_ancestor_;
  /** Sum of user time in microseconds for all forked but not exec()-ed children */
  int64_t utime_u_ = 0;
  /** Sum of system time in microseconds for all forked but not exec()-ed children */
  int64_t stime_u_ = 0;
  /** Sum of user and system time in microseconds for all finalized exec()-ed children */
  int64_t children_cpu_time_u_ = 0;
  /** Directory the process exec()-started in */
  const FileName* initial_wd_;
  /** Working directories visited by the process and all fork()-children */
  tsl::hopscotch_set<const FileName*> wds_;
  /** Working directories the process and all fork()-children failed to chdir() to */
  tsl::hopscotch_set<const FileName*> failed_wds_;
  std::vector<std::string> args_;
  /** Environment variables in deterministic (sorted) order. */
  std::vector<std::string> env_vars_;
  /**
   * The executable running. In case of scripts this is the interpreter or in case of invoking
   * an executable via a symlink this is the executable the symlink points to. */
  const FileName* executable_;
  /**
   * The path executed. In case of scripts this is the script's name or in case of invoking
   * executable via a symlink this is the name of the symlink. */
  const FileName* executed_path_;
  /**
   * DSO-s loaded by the linker at process startup, in the same order.
   * (DSO-s later loaded via dlopen(), and DSO-s of descendant processes are registered as regular
   * file open operations.) */
  std::vector<const FileName*> libs_;
  /** File usage per path for p and f. c. (t.) */
  tsl::hopscotch_map<const FileName*, const FileUsage*> file_usages_;
  /**
   * Pipes created by this process.
   */
  tsl::hopscotch_set<std::shared_ptr<Pipe>> created_pipes_ = {};
  /**
   * The outbound pipes this process had at startup.
   * Each such pipe might have multiple client-side file descriptors (see dup() and friends),
   * they are in sorted order. Also, this inherited_outgoing_pipes_ array is sorted according to the
   * first (lowest) fd for each inherited outgoing pipe.
   */
  std::vector<inherited_outgoing_pipe_t> inherited_outgoing_pipes_ = {};
  void store_in_cache();
  ExecedProcess* next_shortcutable_ancestor() {
    if (maybe_shortcutable_ancestor_ == nullptr || maybe_shortcutable_ancestor_->can_shortcut_) {
      return maybe_shortcutable_ancestor_;
    } else {
      ExecedProcess* next = maybe_shortcutable_ancestor_->maybe_shortcutable_ancestor_;
      while (next != nullptr && !next->can_shortcut_)  {
        next = next->maybe_shortcutable_ancestor_;
      }
      maybe_shortcutable_ancestor_ = next;
      return next;
    }
  }
  /** Reason for this process can't be short-cut */
  const char* cant_shortcut_reason_ = nullptr;
  /** Process the event preventing short-cutting happened in */
  const Process *cant_shortcut_proc_ = NULL;
  /**
   * Helper object for storing in / retrieving from cache.
   * NULL if we prefer not to (although probably could)
   * cache / shortcut this process. */
  ExecedProcessCacher *cacher_;
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  /* namespace firebuild */
#endif  // FIREBUILD_EXECED_PROCESS_H_
