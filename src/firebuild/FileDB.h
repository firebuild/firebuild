/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILEDB_H_
#define FIREBUILD_FILEDB_H_

#include <string>
#include <unordered_map>

#include "firebuild/File.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class FileDB: public std::unordered_map<std::string, File*> {
 public:
  static FileDB* getInstance() {
    static FileDB    instance_;   // Guaranteed to be destroyed.
    // Instantiated on first use.
    return &instance_;
  }
 private:
  FileDB() {}
  ~FileDB();
  DISALLOW_COPY_AND_ASSIGN(FileDB);
};

}  // namespace firebuild
#endif  // FIREBUILD_FILEDB_H_
