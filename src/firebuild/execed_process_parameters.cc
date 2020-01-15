/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/execed_process_parameters.h"

namespace firebuild {

ExecedProcessParameters::ExecedProcessParameters() : argv_() { }

void ExecedProcessParameters::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back(cmd);
}

std::string to_string(ExecedProcessParameters const &pp) {
  std::string res = "[";
  bool add_sep = false;
  for (const auto &arg : pp.argv()) {
    if (add_sep) {
      res += ", ";
    }
    res += "\"" + arg + "\"";  // FIXME backslash-escape the special chars
    add_sep = true;
  }
  res += "]";
  return res;
}

}  // namespace firebuild
