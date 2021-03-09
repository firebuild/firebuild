/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXE_MATCHER_H_
#define FIREBUILD_EXE_MATCHER_H_

#include <string>
#include <unordered_set>

#include "firebuild/file_name.h"

namespace firebuild {

/**
 * Class for checking if either exe or arg0 matches any of the base names of full names (paths).
 */
class ExeMatcher {
 public:
  ExeMatcher() : base_names_(), full_names_() {}
  bool match(const firebuild::FileName* exe_file, const std::string& arg0) const {
    const std::string exe = exe_file->to_string();
    size_t pos = exe.rfind('/');
    const std::string exe_base = exe.substr(pos == std::string::npos ? 0 : pos + 1);
    pos = arg0.rfind('/');
    const std::string arg0_base = arg0.substr(pos == std::string::npos ? 0 : pos + 1);
    if (base_names_.find(exe_base) != base_names_.end()
        || base_names_.find(arg0_base) != base_names_.end()
        || full_names_.find(exe) != full_names_.end()
        || full_names_.find(arg0) != full_names_.end()) {
      return true;
    }
    return false;
  }
  void add(const std::string name) {
    if (name.find('/') == std::string::npos) {
      base_names_.insert(name);
    } else {
      full_names_.insert(name);
    }
  }

 private:
  std::unordered_set<std::string> base_names_;
  std::unordered_set<std::string> full_names_;
};

}  // namespace firebuild
#endif  // FIREBUILD_EXE_MATCHER_H_
