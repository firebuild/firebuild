
#ifndef FB_EXECED_PROCESS_H
#define FB_EXECED_PROCESS_H

#include "Process.h"

#include "fb-messages.pb.h"

using namespace std;

namespace firebuild 
{
  
class ExecedProcess : public Process
{
 public:
  Process *exec_parent = NULL;
  string cwd;
  vector<string> args;
  set<string> env_vars;
  string executable;
  ExecedProcess (firebuild::msg::ShortCutProcessQuery const & scpq);
};


}
#endif
