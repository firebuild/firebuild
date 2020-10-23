/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <memory>

#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"

namespace firebuild {

ExecedProcessEnv::ExecedProcessEnv()
    : argv_(), launch_type_(LAUNCH_TYPE_OTHER), fds_(nullptr) { }

ExecedProcessEnv::ExecedProcessEnv(
    std::shared_ptr<std::unordered_map<int, std::shared_ptr<FileFD>>> fds)
    : argv_(), launch_type_(LAUNCH_TYPE_OTHER), fds_(fds) { }

void ExecedProcessEnv::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back(cmd);
}

std::string to_string(ExecedProcessEnv const &pp) {
  return pretty_print_array(pp.argv());
}

}  // namespace firebuild
