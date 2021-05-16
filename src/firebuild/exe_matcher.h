/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXE_MATCHER_H_
#define FIREBUILD_EXE_MATCHER_H_

#include <string>
#include <string_view>
#include <unordered_set>

#include "firebuild/file_name.h"

namespace firebuild {

/**
 * Class for checking if either exe or arg0 matches any of the base names of full names (paths).
 */
class ExeMatcher {
 public:
  ExeMatcher() : base_names_(), full_names_(), backing_strings() {}
  bool match(const firebuild::FileName* exe_file, const firebuild::FileName* executed_file,
             const std::string_view& arg0) const {
    return match(exe_file->to_string()) || match(arg0)
        || (executed_file == exe_file ? false
            : (executed_file ? match(executed_file->to_string()) : false));
  }
  void add(const std::string& name) {
    const std::string& backing_string = *backing_strings.insert(name).first;
    if (backing_string.find('/') == std::string::npos) {
      base_names_.insert(backing_string);
    } else {
      full_names_.insert(backing_string);
    }
  }

 private:
  bool match(const std::string_view& exe) const {
    size_t pos = exe.rfind('/');
    const std::string_view exe_base = exe.substr(pos == std::string::npos ? 0 : pos + 1);
    return base_names_.find(exe_base) != base_names_.end()
        || full_names_.find(exe) != full_names_.end();
  }
  std::unordered_set<std::string_view> base_names_;
  std::unordered_set<std::string_view> full_names_;
  std::unordered_set<std::string> backing_strings;
};

}  // namespace firebuild
#endif  // FIREBUILD_EXE_MATCHER_H_
