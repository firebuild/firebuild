/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/execed_process_parameters.h"

#include "firebuild/debug.h"

namespace firebuild {

ExecedProcessParameters::ExecedProcessParameters() : argv_(), fds_(nullptr) { }

ExecedProcessParameters::ExecedProcessParameters(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds)
    : argv_(), fds_(fds) { }

void ExecedProcessParameters::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back(cmd);
}

std::string to_string(ExecedProcessParameters const &pp) {
  return pretty_print_array(pp.argv());
}

}  // namespace firebuild
