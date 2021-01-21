/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <string>

#include "firebuild/file_name.h"
#include "firebuild/process_factory.h"

namespace firebuild {

class ExecedProcessCacher;

ForkedProcess*
ProcessFactory::getForkedProcess(const int pid, Process * const parent) {
  TRACK(FB_DEBUG_PROC, "pid=%d, parent=%s", pid, D(parent));

  return new ForkedProcess(pid, parent->pid(), parent, parent->pass_on_fds(false));
}

ExecedProcess*
ProcessFactory::getExecedProcess(const FBB_scproc_query *msg, Process * parent,
                                 std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds) {
  TRACK(FB_DEBUG_PROC, "parent=%s", D(parent));

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

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created ExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(e->executable()));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(e->args()));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(e->initial_wd()));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(e->env_vars()));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(fbb_scproc_query_get_libs(msg)));

  return e;
}

}  // namespace firebuild
