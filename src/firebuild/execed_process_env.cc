/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <memory>

#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"

namespace firebuild {

ExecedProcessEnv::ExecedProcessEnv()
    : argv_(), launch_type_(LAUNCH_TYPE_OTHER), fds_(nullptr), file_actions_(nullptr) { }

ExecedProcessEnv::ExecedProcessEnv(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds,
                                   std::vector<std::string> file_actions)
    : argv_(), launch_type_(LAUNCH_TYPE_OTHER), fds_(fds),
      file_actions_(std::make_shared<std::vector<std::string>>(file_actions)) { }

void ExecedProcessEnv::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back(cmd);
}

std::string to_string(ExecedProcessEnv const &pp) {
  return pretty_print_array(pp.argv());
}

}  // namespace firebuild
