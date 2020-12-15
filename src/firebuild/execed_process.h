/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECED_PROCESS_H_
#define FIREBUILD_EXECED_PROCESS_H_

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "firebuild/file_name.h"
#include "firebuild/file_usage.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"

namespace firebuild {

class ExecedProcessCacher;

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const FileName *initial_wd,
                         const FileName *executable, Process * parent,
                         std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds);
  virtual ~ExecedProcess();
  virtual bool exec_started() const {return true;}
  ExecedProcess* exec_point() {return this;}
  const ExecedProcess* exec_point() const {return this;}
  int64_t sum_utime_u() const {return sum_utime_u_;}
  void set_sum_utime_u(int64_t t) {sum_utime_u_ = t;}
  int64_t sum_stime_u() const {return sum_stime_u_;}
  void set_sum_stime_u(int64_t t) {sum_stime_u_ = t;}
  const FileName* initial_wd() const {return initial_wd_;}
  const std::unordered_set<const FileName*>& wds() const {return wds_;}
  const std::unordered_set<const FileName*>& wds() {return wds_;}
  const std::unordered_set<const FileName*>& failed_wds() const {return wds_;}
  std::unordered_set<const FileName*>& failed_wds() {return failed_wds_;}
  const std::vector<std::string>& args() const {return args_;}
  std::vector<std::string>& args() {return args_;}
  void set_args(const std::vector<std::string>& args) {args_ = args;}
  const std::vector<std::string>& env_vars() const {return env_vars_;}
  std::vector<std::string>& env_vars() {return env_vars_;}
  void set_env_vars(const std::vector<std::string>& env_vars) {env_vars_ = env_vars;}
  const FileName* executable() const {return executable_;}
  std::vector<const FileName*>& libs() {return libs_;}
  const std::vector<const FileName*>& libs() const {return libs_;}
  void set_libs(std::vector<const FileName*> libs) {libs_ = libs;}
  std::unordered_map<const FileName*, FileUsage*>& file_usages() {return file_usages_;}
  const std::unordered_map<const FileName*, FileUsage*>& file_usages() const {return file_usages_;}
  void set_cacher(ExecedProcessCacher *cacher) {cacher_ = cacher;}
  void do_finalize();
  Process* exec_proc() const {return const_cast<ExecedProcess*>(this);}
  void exit_result(const int status, const int64_t utime_u,
                   const int64_t stime_u);

  void initialize();
  void propagate_file_usage(const FileName *name,
                            const FileUsage &fu_change);
  bool register_file_usage(const FileName *name, const FileName *actual_file,
                           FileAction action, int flags, int error);
  bool register_file_usage(const FileName *name, FileUsage fu_change);

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

  /// Returns if the process can be short-cut
  bool can_shortcut() const {return can_shortcut_;}

  bool shortcut();

  virtual void propagate_exit_status(const int status);
  virtual void disable_shortcutting_only_this(const std::string &reason, const Process *p = NULL) {
    if (can_shortcut_) {
      can_shortcut_ = false;
      assert(cant_shortcut_reason_ == "");
      cant_shortcut_reason_ = reason;
      assert(cant_shortcut_proc_ == NULL);
      cant_shortcut_proc_ = p ? p : this;
      FB_DEBUG(FB_DEBUG_PROC, "Command \"" + std::string(executable_->c_str())
               + "\" can't be short-cut due to: " + reason);
    }
  }
  bool was_shortcut() const {return was_shortcut_;}
  void set_was_shortcut(bool value) {was_shortcut_ = value;}
  virtual int64_t sum_rusage_recurse();

  void export2js(const unsigned int level, FILE* stream,
                 unsigned int * nodeid);
  void export2js_recurse(const unsigned int level, FILE* stream,
                         unsigned int *nodeid);

 private:
  bool can_shortcut_:1;
  bool was_shortcut_:1;
  /// Sum of user time in microseconds for all forked but not exec()-ed children
  int64_t sum_utime_u_ = 0;
  /// Sum of system time in microseconds for all forked but not exec()-ed
  /// children
  int64_t sum_stime_u_ = 0;
  /// Directory the process exec()-started in
  const FileName* initial_wd_;
  /// Working directories visited by the process and all fork()-children
  std::unordered_set<const FileName*> wds_;
  /// Working directories the process and all fork()-children failed to
  /// chdir() to
  std::unordered_set<const FileName*> failed_wds_;
  std::vector<std::string> args_;
  /// Environment variables in deterministic (sorted) order.
  std::vector<std::string> env_vars_;
  const FileName* executable_;
  /// DSO-s loaded by the linker at process startup, in the same order.
  /// (DSO-s later loaded via dlopen(), and DSO-s of descendant processes
  /// are registered as regular file open operations.)
  std::vector<const FileName*> libs_;
  /// File usage per path for p and f. c. (t.)
  std::unordered_map<const FileName*, FileUsage*> file_usages_;
  void store_in_cache();
  /// Reason for this process can't be short-cut
  std::string cant_shortcut_reason_ = "";
  /// Process the event preventing short-cutting happened in
  const Process *cant_shortcut_proc_ = NULL;
  /// Helper object for storing in / retrieving from cache.
  /// NULL if we prefer not to (although probably could)
  /// cache / shortcut this process.
  ExecedProcessCacher *cacher_;
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_H_
