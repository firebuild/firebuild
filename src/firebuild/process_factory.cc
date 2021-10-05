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
ProcessFactory::getExecedProcess(const FBBCOMM_Serialized_scproc_query *msg, Process * parent,
                                 std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds) {
  TRACK(FB_DEBUG_PROC, "parent=%s", D(parent));

  const FileName* executable = FileName::Get(fbbcomm_serialized_scproc_query_get_executable(msg));
  const FileName* executed_path = fbbcomm_serialized_scproc_query_has_executed_path(msg)
      ? FileName::Get(fbbcomm_serialized_scproc_query_get_executed_path(msg)) : nullptr;
  auto e = new ExecedProcess(fbbcomm_serialized_scproc_query_get_pid(msg),
                             fbbcomm_serialized_scproc_query_get_ppid(msg),
                             FileName::Get(fbbcomm_serialized_scproc_query_get_cwd(msg)),
                             executable, executed_path,
                             parent, fds);

  std::vector<std::string> args = fbbcomm_serialized_scproc_query_get_arg_as_vector(msg);
  e->set_args(args);
  std::vector<std::string> env_vars = fbbcomm_serialized_scproc_query_get_env_var_as_vector(msg);
  e->set_env_vars(env_vars);

  auto& libs = e->libs();
  for (uint32_t i = 0; i < fbbcomm_serialized_scproc_query_get_libs_count(msg); i++) {
    libs.push_back(FileName::Get(fbbcomm_serialized_scproc_query_get_libs_at(msg, i)));
  }

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created ExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(e->executable()));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(e->args()));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(e->initial_wd()));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(e->env_vars()));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(fbbcomm_serialized_scproc_query_get_libs_as_vector(msg)));

  return e;
}

}  // namespace firebuild
