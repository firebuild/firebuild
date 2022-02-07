/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_H_
#define FIREBUILD_FILE_H_

#include <time.h>

#include <string>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class File {
 public:
  explicit File(const FileName* path);
  int update();
  int is_changed();
  const FileName* path() const {return path_;}
  bool exists() const {return exists_;}
  const Hash& hash() const {return hash_;}

 private:
  std::vector<timespec> mtimes_;
  const FileName* path_;
  bool exists_;
  Hash hash_;
  int set_hash();
  DISALLOW_COPY_AND_ASSIGN(File);
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const File& f, const int level = 0);
std::string d(const File *f, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_FILE_H_
