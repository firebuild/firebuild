
#include "ProcessFactory.h"

namespace firebuild {

ForkedProcess* ProcessFactory::getForkedProcess(const msg::ForkChild &fc,
                                                Process * const fork_parent) {
  auto f = new ForkedProcess(fc.pid(), fc.ppid(), fork_parent);
  return f;

}

ExecedProcess*
ProcessFactory::getExecedProcess(const msg::ShortCutProcessQuery &scpq) {
  auto e = new ExecedProcess(scpq.pid(), scpq.ppid(), scpq.cwd(),
                             scpq.executable());

  for (int i = 0; i < scpq.arg_size(); i++) {
    e->args().push_back(scpq.arg(i));
  }
  for (int i = 0; i < scpq.env_var_size(); i++) {
    const char *fb_socket = "FB_SOCKET=";
    if ( 0 == strncmp(scpq.env_var(i).c_str(), fb_socket, strlen(fb_socket))) {
      // this is used internally by FireBuild and changes with every run
      continue;
    }
    e->env_vars().insert(scpq.env_var(i));
  }
  // TODO keep files in a separate container and refer to them instead of
  // creating the same strings several times
  for (int i = 0; i < scpq.libs().file_size(); i++) {
    e->libs().insert(scpq.libs().file(i));
  }
  return e;
}

}  // namespace firebuild
