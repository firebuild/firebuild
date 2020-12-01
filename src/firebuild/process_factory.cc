/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <string>

#include "firebuild/file_name.h"
#include "firebuild/process_factory.h"

namespace firebuild {

class ExecedProcessCacher;

ForkedProcess*
ProcessFactory::getForkedProcess(const int pid, Process * const parent) {
  return new ForkedProcess(pid, parent->pid(), parent, parent->pass_on_fds(false));
}

ExecedProcess*
ProcessFactory::getExecedProcess(const FBB_scproc_query *msg, Process * parent,
                                 std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds) {
  auto e = new ExecedProcess(fbb_scproc_query_get_pid(msg),
                             fbb_scproc_query_get_ppid(msg),
                             FileName::Get(fbb_scproc_query_get_cwd(msg)),
                             FileName::Get(fbb_scproc_query_get_executable(msg)),
                             parent, fds);

  std::vector<std::string> args = fbb_scproc_query_get_arg(msg);
  e->set_args(args);
  std::vector<std::string> env_vars = fbb_scproc_query_get_env_var(msg);
  e->set_env_vars(env_vars);

  auto& libs = e->libs();
  for_s_in_fbb_scproc_query_libs(msg, {libs.push_back(FileName::Get(s, s_length));});

  return e;
}

}  // namespace firebuild
