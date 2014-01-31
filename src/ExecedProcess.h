
#ifndef FIREBUILD_EXECED_PROCESS_H
#define FIREBUILD_EXECED_PROCESS_H

#include "Process.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{
  
class ExecedProcess : public Process
{
 public:
  explicit ExecedProcess (const int pid, const int ppid, const std::string &cwd,
                          const std::string &executable);
  virtual ~ExecedProcess();
  void set_exec_parent(Process *p) {exec_parent_ = p;};
  Process* exec_parent() {return exec_parent_;};
  long int sum_utime_m() {return sum_utime_m_;}
  void set_sum_utime_m(long int t) {sum_utime_m_ = t;}
  long int sum_stime_m() {return sum_stime_m_;}
  void set_sum_stime_m(long int t) {sum_stime_m_ = t;}
  std::string& cwd() {return cwd_;};
  //  void set_cwd(std::string &c) {cwd_ = c;};
  std::set<std::string>& wds() {return wds_;};
  std::set<std::string>& failed_wds() {return failed_wds_;};
  std::vector<std::string>& args() {return args_;}
  std::set<std::string>& env_vars() {return env_vars_;}
  std::string& executable() {return executable_;};
  std::set<std::string>& libs() {return libs_;};
  std::unordered_map<std::string, FileUsage*>& file_usages() {return file_usages_;};
  void exit_result (const int status, const long int utime_m, const long int stime_m);
  void export2js(const unsigned int level, std::ostream& o);
  /**
   * Fail to change to a working directory
   */
  void fail_wd(const std::string &d)
  {
    failed_wds_.insert(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const std::string &d)
  {
    wds_.insert(d);
  }

 private:
  Process *exec_parent_ = NULL;
  long int sum_utime_m_ = 0; /**< Sum of user time in milliseconds for all forked
                               but not exec()-ed children */
  long int sum_stime_m_ = 0; /**< Sum of system time in milliseconds for all
                               forked but not exec()-ed children */
  std::string cwd_; /**< Directory the process exec()-started in */
  std::set<std::string> wds_; /**< Working directories visited by the process
                               * and all fork()-children */
  std::set<std::string> failed_wds_; /** < Working directories the process and
                                      * all fork()-children failed to chdir() to */
  std::vector<std::string> args_;
  std::set<std::string> env_vars_;
  std::string executable_;
  std::set<std::string> libs_; ///< DSO-s loaded by process and forked children (transitively)
  std::unordered_map<std::string, FileUsage*> file_usages_; ///< File usage per path for p and f. c. (t.)
  void propagate_exit_status (const int status);
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}
#endif
