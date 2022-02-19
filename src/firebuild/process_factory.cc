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

  const FileName* executable = FileName::Get(fbbcomm_serialized_scproc_query_get_executable(msg));
  const FileName* executed_path = fbbcomm_serialized_scproc_query_has_executed_path(msg)
      ? FileName::Get(fbbcomm_serialized_scproc_query_get_executed_path(msg)) : nullptr;
  const size_t libs_count = fbbcomm_serialized_scproc_query_get_libs_count(msg);
  std::vector<const FileName*> libs(libs_count);
  for (size_t i = 0; i < libs_count; i++) {
    libs[i] = (FileName::Get(fbbcomm_serialized_scproc_query_get_libs_at(msg, i),
                             fbbcomm_serialized_scproc_query_get_libs_len_at(msg, i)));
  }
  auto e = new ExecedProcess(fbbcomm_serialized_scproc_query_get_pid(msg),
                             fbbcomm_serialized_scproc_query_get_ppid(msg),
                             FileName::Get(fbbcomm_serialized_scproc_query_get_cwd(msg)),
                             executable, executed_path,
                             fbbcomm_serialized_scproc_query_get_arg_as_vector(msg),
                             fbbcomm_serialized_scproc_query_get_env_var_as_vector(msg),
                             std::move(libs), parent, fds);

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created ExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(e->executable()));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(e->args()));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(e->initial_wd()));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(e->env_vars()));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(fbbcomm_serialized_scproc_query_get_libs_as_vector(msg)));

  return e;
}

IncompleteExecedProcess*
ProcessFactory::getIncompleteExecedProcess(const FBBCOMM_Serialized_scproc_query *const msg) {
  TRACK(FB_DEBUG_PROC, "");

  const FileName* executable = FileName::Get(fbbcomm_serialized_scproc_query_get_executable(msg));
  const FileName* executed_path = fbbcomm_serialized_scproc_query_has_executed_path(msg)
      ? FileName::Get(fbbcomm_serialized_scproc_query_get_executed_path(msg)) : nullptr;
  const size_t libs_count = fbbcomm_serialized_scproc_query_get_libs_count(msg);
  std::vector<const FileName*> libs(libs_count);
  for (size_t i = 0; i < libs_count; i++) {
    libs[i] = (FileName::Get(fbbcomm_serialized_scproc_query_get_libs_at(msg, i),
                             fbbcomm_serialized_scproc_query_get_libs_len_at(msg, i)));
  }
  auto e = new IncompleteExecedProcess(fbbcomm_serialized_scproc_query_get_pid(msg),
                                       fbbcomm_serialized_scproc_query_get_ppid(msg),
                                       FileName::Get(fbbcomm_serialized_scproc_query_get_cwd(msg)),
                                       executable, executed_path,
                                       fbbcomm_serialized_scproc_query_get_arg_as_vector(msg),
                                       fbbcomm_serialized_scproc_query_get_env_var_as_vector(msg),
                                       std::move(libs));

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created IncompleteExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(executable));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(fbbcomm_serialized_scproc_query_get_arg_as_vector(msg)));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(FileName::Get(fbbcomm_serialized_scproc_query_get_cwd(msg))));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(fbbcomm_serialized_scproc_query_get_env_var_as_vector(msg)));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(fbbcomm_serialized_scproc_query_get_libs_as_vector(msg)));

  return e;
}

}  /* namespace firebuild */
