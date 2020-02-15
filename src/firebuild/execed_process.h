/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECEDPROCESS_H_
#define FIREBUILD_EXECEDPROCESS_H_

#include <cassert>
#include <set>
#include <string>
#include <vector>

#include "firebuild/file_usage.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"
#include "firebuild/fb-cache.pb.h"

namespace firebuild {

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const std::string &cwd,
                         const std::string &executable, Process * parent);
  virtual ~ExecedProcess();
  virtual bool exec_started() const {return true;}
  ExecedProcess* exec_point() {return this;}
  const ExecedProcess* exec_point() const {return this;}
  int64_t sum_utime_u() const {return sum_utime_u_;}
  void set_sum_utime_u(int64_t t) {sum_utime_u_ = t;}
  int64_t sum_stime_u() const {return sum_stime_u_;}
  void set_sum_stime_u(int64_t t) {sum_stime_u_ = t;}
  const std::string& cwd() const {return cwd_;}
  std::string& cwd() {return cwd_;}
  const std::set<std::string>& wds() const {return wds_;}
  std::set<std::string>& wds() {return wds_;}
  const std::set<std::string>& failed_wds() const {return wds_;}
  std::set<std::string>& failed_wds() {return failed_wds_;}
  const std::vector<std::string>& args() const {return args_;}
  std::vector<std::string>& args() {return args_;}
  const std::vector<std::string>& env_vars() const {return env_vars_;}
  std::vector<std::string>& env_vars() {return env_vars_;}
  const std::string& executable() const {return executable_;}
  std::string& executable() {return executable_;}
  const std::vector<std::string>& libs() const {return libs_;}
  std::vector<std::string>& libs() {return libs_;}
  std::unordered_map<std::string, FileUsage*>& file_usages() {return file_usages_;}
  Process* exec_proc() const {return const_cast<ExecedProcess*>(this);};
  void exit_result(const int status, const int64_t utime_u,
                   const int64_t stime_u);
  void set_fingerprint(const Hash& fingerprint) {fingerprint_ = fingerprint;}
  const Hash& fingerprint() const {return fingerprint_;}
  void set_fingerprint_msg(firebuild::msg::ProcessFingerprint *fingerprint_msg) {delete fingerprint_msg_; fingerprint_msg_ = fingerprint_msg;}
  const firebuild::msg::ProcessFingerprint *fingerprint_msg() const {return fingerprint_msg_;}

  void propagate_file_usage(const std::string &name,
                            const FileUsage &fu_change);
  bool register_file_usage(const std::string &name, const int flags, const int error);

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
  virtual void disable_shortcutting(const std::string &reason, const Process *p = NULL) {
    if (true == can_shortcut_) {
      can_shortcut_ = false;
      assert(cant_shortcut_reason_ == "");
      cant_shortcut_reason_ = reason;
      assert(cant_shortcut_proc_ == NULL);
      cant_shortcut_proc_ = p ? p : this;
      FB_DEBUG(FB_DEBUG_PROC, "Command \"" + executable_ + "\" can't be short-cut due to: " + reason);
      if (parent()) {
        parent()->disable_shortcutting(reason, cant_shortcut_proc_);
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
  /// Sum of user time in microseconds for all forked but not exec()-ed children
  int64_t sum_utime_u_ = 0;
  /// Sum of system time in microseconds for all forked but not exec()-ed
  /// children
  int64_t sum_stime_u_ = 0;
  /// Directory the process exec()-started in
  std::string cwd_;
  /// Working directories visited by the process and all fork()-children
  std::set<std::string> wds_;
  /// Working directories the process and all fork()-children failed to
  /// chdir() to
  std::set<std::string> failed_wds_;
  std::vector<std::string> args_;
  /// Environment variables in deterministic (sorted) order.
  std::vector<std::string> env_vars_;
  std::string executable_;
  /// DSO-s loaded by the linker at process startup, in the same order.
  /// (DSO-s later loaded via dlopen(), and DSO-s of descendant processes
  /// are registered as regular file open operations.)
  std::vector<std::string> libs_;
  /// File usage per path for p and f. c. (t.)
  std::unordered_map<std::string, FileUsage*> file_usages_;
  /// Fingerprint of the process
  Hash fingerprint_;
  /// Fingerprint of the process, as the entire protobuf, for debugging purposes
  firebuild::msg::ProcessFingerprint *fingerprint_msg_;
  /// Reason for this process can't be short-cut
  std::string cant_shortcut_reason_ = "";
  /// Process the event preventing short-cutting happened in
  const Process *cant_shortcut_proc_ = NULL;
  virtual bool can_shortcut() const {return can_shortcut_;}
  virtual bool can_shortcut() {return can_shortcut_;}
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_EXECEDPROCESS_H_
