/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <string>
#include <utility>

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
ProcessFactory::getExecedProcess(const FBBCOMM_Serialized_scproc_query *const msg,
                                 Process * const parent,
                                 std::vector<std::shared_ptr<FileFD>>* fds) {
  TRACK(FB_DEBUG_PROC, "parent=%s", D(parent));

  const FileName* executable = FileName::Get(msg->get_executable());
  const FileName* executed_path = msg->has_executed_path()
      ? FileName::Get(msg->get_executed_path()) : nullptr;
  const size_t libs_count = msg->get_libs_count();
  std::vector<const FileName*> libs(libs_count);
  for (size_t i = 0; i < libs_count; i++) {
    libs[i] = (FileName::Get(msg->get_libs_at(i), msg->get_libs_len_at(i)));
  }
  auto e = new ExecedProcess(msg->get_pid(),
                             msg->get_ppid(),
                             FileName::Get(msg->get_cwd()),
                             executable, executed_path,
                             msg->get_arg_as_vector(),
                             msg->get_env_var_as_vector(),
                             std::move(libs), parent, fds);

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created ExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(e->executable()));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(e->args()));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(e->initial_wd()));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(e->env_vars()));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(msg->get_libs_as_vector()));

  return e;
}

}  /* namespace firebuild */
