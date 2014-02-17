/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECEDPROCESS_H_
#define FIREBUILD_EXECEDPROCESS_H_

#include <set>
#include <string>
#include <vector>

#include "Process.h"
#include "cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const std::string &cwd,
                          const std::string &executable);
  virtual ~ExecedProcess();
  void set_exec_parent(Process *p) {exec_parent_ = p;}
  Process* exec_parent() {return exec_parent_;}
  long int sum_utime_m() const {return sum_utime_m_;}
  void set_sum_utime_m(long int t) {sum_utime_m_ = t;}
  long int sum_stime_m() const {return sum_stime_m_;}
  void set_sum_stime_m(long int t) {sum_stime_m_ = t;}
  const std::string& cwd() const {return cwd_;}
  std::string& cwd() {return cwd_;}
  //  void set_cwd(std::string &c) {cwd_ = c;}
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
    return const_cast<std::unordered_map<std::string, FileUsage*>&>(static_cast<const ExecedProcess*>(this)->file_usages());
  }
  void exit_result(const int status, const long int utime_m,
                   const long int stime_m);
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

 private:
  Process *exec_parent_ = NULL;
  /// Sum of user time in milliseconds for all forked but not exec()-ed children
  long int sum_utime_m_ = 0;
  /// Sum of system time in milliseconds for all forked but not exec()-ed
  /// children
  long int sum_stime_m_ = 0;
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
  void propagate_exit_status(const int status);
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_EXECEDPROCESS_H_
