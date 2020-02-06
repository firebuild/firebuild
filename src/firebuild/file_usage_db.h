/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILEUSAGEDB_H_
#define FIREBUILD_FILEUSAGEDB_H_

#include <string>
#include <unordered_map>

#include "firebuild/file_usage.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class FileUsageDB {
 public:
  static FileUsageDB* getInstance() {
    static FileUsageDB    instance_;   // Guaranteed to be destroyed.
    // Instantiated on first use.
    return &instance_;
  }
  size_t count(const std::string& key) {
    return db_.count(key);
  }
  FileUsage*& operator[](const std::string& key) {
    return db_[key];
  }
 private:
  std::unordered_map<std::string, FileUsage*> db_ = {};
  FileUsageDB() {}
  ~FileUsageDB();
  DISALLOW_COPY_AND_ASSIGN(FileUsageDB);
};

}  // namespace firebuild
#endif  // FIREBUILD_FILEUSAGEDB_H_
