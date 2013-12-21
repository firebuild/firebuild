
#ifndef FIREBUILD_EXECED_PROCESS_H
#define FIREBUILD_EXECED_PROCESS_H

#include "Process.h"
#include "fb-messages.pb.h"

namespace firebuild 
{
  
class ExecedProcess : public Process
{
 private:
  void propagate_exit_status (const int status);
 public:
  Process *exec_parent = NULL;
  long int sum_utime_m = 0; /**< Sum of user time in milliseconds for all forked
                               but not exec()-ed children */
  long int sum_stime_m = 0; /**< Sum of system time in milliseconds for all
                               forked but not exec()-ed children */
  std::string cwd;
  std::vector<std::string> args;
  std::set<std::string> env_vars;
  std::string executable;
  /** Process can be shortcut by FireBuild. */
  bool shortcuttable = true;
  ExecedProcess (firebuild::msg::ShortCutProcessQuery const & scpq);
  void exit_result (const int status, const long int utime_m, const long int stime_m);
};


}
#endif
