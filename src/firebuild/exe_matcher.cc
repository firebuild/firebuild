/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/exe_matcher.h"

#include <tsl/hopscotch_set.h>

#include <string>

#include "firebuild/execed_process.h"
#include "firebuild/file_name.h"

namespace firebuild {

bool ExeMatcher::match(const ExecedProcess* const proc) const {
  return match(proc->executable(), proc->executed_path(),
               proc->args().size() > 0 ? proc->args()[0] : "");
}

bool ExeMatcher::match(const FileName* exe_file, const FileName* executed_file,
                       const std::string& arg0) const {
  return match(exe_file->to_string()) || match(arg0)
      || (executed_file == exe_file ? false
          : (executed_file ? match(executed_file->to_string()) : false));
}

bool ExeMatcher::match(const std::string& exe) const {
  size_t pos = exe.rfind('/');
  const std::string exe_base = exe.substr(pos == std::string::npos ? 0 : pos + 1);
  return base_names_.find(exe_base) != base_names_.end()
      || full_names_.find(exe) != full_names_.end();
}

}  /* namespace firebuild */