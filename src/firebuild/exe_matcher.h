/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXE_MATCHER_H_
#define FIREBUILD_EXE_MATCHER_H_

#include <tsl/hopscotch_set.h>

#include <string>

#include "firebuild/file_name.h"

namespace firebuild {

class ExecedProcess;

/**
 * Class for checking if either exe or arg0 matches any of the base names of full names (paths).
 */
class ExeMatcher {
 public:
  ExeMatcher() : base_names_(), full_names_() {}
  bool match(const ExecedProcess* const proc) const;
  bool match(const FileName* exe_file, const FileName* executed_file,
             const std::string& arg0) const;
  void add(const std::string name) {
    if (name.find('/') == std::string::npos) {
      base_names_.insert(name);
    } else {
      full_names_.insert(name);
    }
  }

 private:
  bool match(const std::string& exe) const;
  tsl::hopscotch_set<std::string> base_names_;
  tsl::hopscotch_set<std::string> full_names_;
};

}  /* namespace firebuild */
#endif  // FIREBUILD_EXE_MATCHER_H_
