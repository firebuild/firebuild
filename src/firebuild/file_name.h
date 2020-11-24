/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_NAME_H_
#define FIREBUILD_FILE_NAME_H_

#include <flatbuffers/flatbuffers.h>

#include <cstring>
#include <string>
#include <unordered_set>

#include "firebuild/platform.h"

namespace firebuild {

struct FileNameHasher;
class FileName {
 public:
  FileName(const FileName& other)
      : name_(strdup(other.name_)) {}
  const char * c_str() const {return name_;}
  std::string to_string() const {return std::string(name_);}
  size_t hash() const {return std::_Hash_bytes(name_, strlen(name_), 0);}
  static const FileName* Get(const char * const name);
  static const FileName* Get(const flatbuffers::String * const name) {return Get(name->c_str());}
  /**
   * Prepend dir to name (using the directory separator) if name is not absolute, otherwise return name.
   */
  static const FileName* GetAbsolute(const FileName * const dir, const char * const name);

 private:
  FileName(const char * const name, bool copy_name)
      : name_(copy_name ? strdup(name) : name) {}
  const char * name_;
  static std::unordered_set<FileName, FileNameHasher>* db_;
  /* Disable assignment. */
  void operator=(const FileName&);

  /* This, along with the FileName::db_initializer_ definition in file_namedb.cc,
   * initializes the filename database once at startup. */
  class DbInitializer {
   public:
    DbInitializer();
  };
  friend class DbInitializer;
  static DbInitializer db_initializer_;
};

inline bool operator==(const FileName& lhs, const FileName& rhs) {
  return strcmp(lhs.c_str(), rhs.c_str()) == 0;
}

struct FileNameHasher {
  std::size_t operator()(const FileName& s) const noexcept {
    return s.hash();
  }
};

inline const FileName* FileName::Get(const char * const name) {
  FileName tmp_file_name(name, false);
  auto it = db_->find(tmp_file_name);
  if (it != db_->end()) {
    return &*it;
  } else {
    /* Not found, add a copy to the set. */
    return &*db_->insert(tmp_file_name).first;
  }
}

inline const FileName* FileName::GetAbsolute(const FileName * const dir, const char * const name) {
  if (platform::path_is_absolute(name)) {
    return Get(name);
  } else {
    char on_stack_buf[2048], *buf;
    const size_t on_stack_buffer_size = sizeof(on_stack_buf);
    const size_t total_buf_len = strlen(dir->name_) + 1 + strlen(name) + 1;
    if (on_stack_buffer_size < total_buf_len) {
      buf = reinterpret_cast<char *>(malloc(total_buf_len));
    } else {
      buf = reinterpret_cast<char *>(on_stack_buf);
    }
    char *buf_ptr = buf;
    buf_ptr = stpcpy(buf_ptr, dir->name_);
    buf_ptr = stpcpy(buf_ptr, "/");
    stpcpy(buf_ptr, name);
    const FileName* ret = Get(buf);
    if (on_stack_buffer_size < total_buf_len) {
      free(buf);
    }
    return ret;
  }
}

}  // namespace firebuild


#endif  // FIREBUILD_FILE_NAME_H_
