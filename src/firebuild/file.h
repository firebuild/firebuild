/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_H_
#define FIREBUILD_FILE_H_

#include <time.h>

#include <string>
#include <vector>

#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class File {
 public:
  explicit File(const std::string &path);
  int update();
  int is_changed();
  std::string& path() {return path_;}
  Hash& hash() {return hash_;}

 private:
  std::vector<timespec> mtimes_;
  std::string path_;
  bool exists_;
  Hash hash_;
  int update_hash();
  DISALLOW_COPY_AND_ASSIGN(File);
};

}  // namespace firebuild
#endif  // FIREBUILD_FILE_H_
