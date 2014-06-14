/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECEDPROCESS_H_
#define FIREBUILD_EXECEDPROCESS_H_

#include <cassert>
#include <set>
#include <string>
#include <vector>

#include "firebuild/Process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/Debug.h"

namespace firebuild {

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const std::string &cwd,
                         const std::string &executable, Process * exec_parent);
  virtual ~ExecedProcess();
  virtual bool exec_started() const {return true;}
  void set_exec_parent(Process *p) {exec_parent_ = p;}
  Process* exec_parent() {return exec_parent_;}
  int64_t sum_utime_m() const {return sum_utime_m_;}
  void set_sum_utime_m(int64_t t) {sum_utime_m_ = t;}
  int64_t sum_stime_m() const {return sum_stime_m_;}
  void set_sum_stime_m(int64_t t) {sum_stime_m_ = t;}
  const std::string& cwd() const {return cwd_;}
  std::string& cwd() {return cwd_;}
  const std::set<std::string>& wds() const {return wds_;}
  std::set<std::string>& wds() {return wds_;}
  const std::set<std::string>& failed_wds() const {return wds_;}
  std::set<std::string>& failed_wds() {return failed_wds_;}
  const std::vector<std::string>& args() const {return args_;}
  std::vector<std::string>& args() {return args_;}
  const std::set<std::string>& env_vars() const {return env_vars_;}
  std::set<std::string>& env_vars() {return env_vars_;}
  const std::string& executable() const {return executable_;}
  std::string& executable() {return executable_;}
  const std::set<std::string>& libs() const {return libs_;}
  std::set<std::string>& libs() {return libs_;}
  const std::unordered_map<std::string, FileUsage*>& file_usages() const {
    return file_usages_;
  }
  std::unordered_map<std::string, FileUsage*>& file_usages() {
    return const_cast<std::unordered_map<std::string, FileUsage*>&>
        (static_cast<const ExecedProcess*>(this)->file_usages());
  }
  Process* exec_proc() const {return const_cast<ExecedProcess*>(this);};
  void exit_result(const int status, const int64_t utime_m,
                   const int64_t stime_m);
  /**
   * Fail to change to a working directory
   */
  void fail_wd(const std::string &d) {
    failed_wds_.insert(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const std::string &d) {
    wds_.insert(d);
  }

  virtual void propagate_exit_status(const int status);
  virtual void disable_shortcutting(const std::string &reason) {
    if (true == can_shortcut_) {
      can_shortcut_ = false;
      assert(cant_shortcut_reason_ == "");
      cant_shortcut_reason_ = reason;
      assert(cant_shortcut_proc_ == NULL);
      cant_shortcut_proc_ = this;
      FB_DEBUG(1, "Command \"" + executable_ + "\" can't be short-cut due to: " + reason);
      if (exec_parent_) {
        exec_parent_->propagate_disable_shortcutting(reason, *this);
      }
    }
  }
  virtual void propagate_disable_shortcutting(const std::string &reason, const Process &p) {
    if (true == can_shortcut_) {
      can_shortcut_ = false;
      assert(cant_shortcut_proc_ == NULL);
      cant_shortcut_proc_ = const_cast<Process*>(&p);
      if (exec_parent_) {
        exec_parent_->propagate_disable_shortcutting(reason, p);
      }
    }
  }
  virtual int64_t sum_rusage_recurse();

  void export2js(const unsigned int level, FILE* stream,
                 unsigned int * nodeid);
  void export2js_recurse(const unsigned int level, FILE* stream,
                         unsigned int *nodeid);

 private:
  bool can_shortcut_:1;
  Process *exec_parent_ = NULL;
  /// Sum of user time in milliseconds for all forked but not exec()-ed children
  int64_t sum_utime_m_ = 0;
  /// Sum of system time in milliseconds for all forked but not exec()-ed
  /// children
  int64_t sum_stime_m_ = 0;
  /// Directory the process exec()-started in
  std::string cwd_;
  /// Working directories visited by the process and all fork()-children
  std::set<std::string> wds_;
  /// Working directories the process and all fork()-children failed to
  /// chdir() to
  std::set<std::string> failed_wds_;
  std::vector<std::string> args_;
  std::set<std::string> env_vars_;
  std::string executable_;
  /// DSO-s loaded by process and forked children (transitively)
  std::set<std::string> libs_;
  /// File usage per path for p and f. c. (t.)
  std::unordered_map<std::string, FileUsage*> file_usages_;
  /// Reason for this process can't be short-cut
  std::string cant_shortcut_reason_ = "";
  /// Process the event preventing short-cutting happened in
  Process *cant_shortcut_proc_ = NULL;
  virtual bool can_shortcut() const {return can_shortcut_;}
  virtual bool can_shortcut() {return can_shortcut_;}
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_EXECEDPROCESS_H_
