
#include "ExecedProcess.h"

using namespace std;
namespace firebuild {

ExecedProcess::ExecedProcess (firebuild::msg::ShortCutProcessQuery const & scpq) 
  : Process(scpq.pid(), scpq.ppid(), FB_PROC_EXEC_STARTED)
{

  cwd = scpq.cwd();
  executable = scpq.executable();
  for (int i = 0; i < scpq.arg_size(); i++) {
    args.push_back(scpq.arg(i));
  }
  for (int i = 0; i < scpq.env_var_size(); i++) {
    const char *fb_socket = "FB_SOCKET="; 
    if ( 0 == strncmp(scpq.env_var(i).c_str(), fb_socket, strlen(fb_socket))) {
      // this is used internally by FireBuild and changes with every run
      continue;
    }
    env_vars.insert(scpq.env_var(i));
  }
  // TODO keep files in a separate container and refer to them instead of
  // creating the same strings several times
  for (int i = 0; i < scpq.libs().file_size(); i++) {
    libs.insert(scpq.libs().file(i));
  }
}

}

