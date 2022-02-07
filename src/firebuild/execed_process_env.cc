/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <memory>

#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"

namespace firebuild {

ExecedProcessEnv::ExecedProcessEnv(std::vector<std::shared_ptr<FileFD>>* fds)
    : argv_(), launch_type_(LAUNCH_TYPE_OTHER), type_flags_(), fds_(fds) { }

void ExecedProcessEnv::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back(cmd);
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const ExecedProcessEnv& env, const int level) {
  (void)level;  /* unused */
  return d(env.argv());
}
std::string d(const ExecedProcessEnv *env, const int level) {
  if (env) {
    return d(*env, level);
  } else {
    return "{ExecedProcessEnv NULL}";
  }
}

}  /* namespace firebuild */
