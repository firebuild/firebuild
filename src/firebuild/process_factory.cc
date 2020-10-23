/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_factory.h"

#include <string>
#include <vector>

namespace firebuild {

class ExecedProcessCacher;

ForkedProcess*
ProcessFactory::getForkedProcess(const int pid, Process * const parent) {
  return new ForkedProcess(pid, parent->pid(), parent, parent->pass_on_fds(false));
}

ExecedProcess*
ProcessFactory::getExecedProcess(
    const FBB_scproc_query *msg, Process * parent,
    std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>> fds) {
  auto e = new ExecedProcess(fbb_scproc_query_get_pid(msg),
                             fbb_scproc_query_get_ppid(msg),
                             fbb_scproc_query_get_cwd(msg),
                             fbb_scproc_query_get_executable(msg),
                             parent, fds);

  std::vector<std::string> args = fbb_scproc_query_get_arg(msg);
  e->set_args(args);
  std::vector<std::string> env_vars = fbb_scproc_query_get_env_var(msg);
  e->set_env_vars(env_vars);
  // TODO(rbalint) keep files in a separate container and refer to them instead
  // of creating the same strings several times
  std::vector<std::string> libs = fbb_scproc_query_get_libs(msg);
  e->set_libs(libs);
  return e;
}

}  // namespace firebuild
