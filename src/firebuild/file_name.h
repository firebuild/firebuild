/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_NAME_H_
#define FIREBUILD_FILE_NAME_H_

#include <flatbuffers/flatbuffers.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "firebuild/platform.h"

namespace firebuild {

struct FileNameHasher;
class FileName {
 public:
  FileName(const FileName& other)
      : name_(reinterpret_cast<const char *>(malloc(other.length_ + 1))),
        length_(other.length_) {
    memcpy(const_cast<char*>(name_), other.name_, other.length_ + 1);
  }
  const char * c_str() const {return name_;}
  std::string to_string() const {return std::string(name_);}
  size_t length() const {return length_;}
  size_t hash() const {return std::_Hash_bytes(name_, length_, 0);}
  static const FileName* Get(const char * const name, ssize_t length);
  static const FileName* Get(const flatbuffers::String * const name) {
    return Get(name->c_str(), name->size());
  }
  /**
   * Prepend dir to name (using the directory separator) if name is not absolute, otherwise return name.
   */
  static const FileName* GetAbsolute(const FileName * const dir, const char * const name,
                                     ssize_t length = -1);
  /**
   * Checks if a path semantically begins with the given subpath.
   *
   * Does string operations only, does not look at the file system.
   */
  bool is_at_locations(const std::vector<const FileName *> *locations) const;

 private:
  FileName(const char * const name, size_t length, bool copy_name)
      : name_(copy_name ? reinterpret_cast<const char *>(malloc(length + 1)) : name),
        length_(length) {
    if (copy_name) {
      memcpy(const_cast<char*>(name_), name, length);
      const_cast<char*>(name_)[length] = '\0';
    }
  }
  const char * name_;
  size_t length_;
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

int FileNamePtrPtrCompare(const FileName * const * const lhs, const FileName * const * const rhs);

inline const FileName* FileName::Get(const char * const name, ssize_t length = -1) {
  FileName tmp_file_name(name, (length == -1) ? strlen(name) : length, false);
  auto it = db_->find(tmp_file_name);
  if (it != db_->end()) {
    return &*it;
  } else {
    /* Not found, add a copy to the set. */
    return &*db_->insert(tmp_file_name).first;
  }
}

inline const FileName* FileName::GetAbsolute(const FileName * const dir, const char * const name,
                                             ssize_t length) {
  if (platform::path_is_absolute(name)) {
    return Get(name, length);
  } else {
    char on_stack_buf[2048], *buf;
    const size_t on_stack_buffer_size = sizeof(on_stack_buf);
    const ssize_t name_length = (length == -1) ? strlen(name) : length;
    const size_t total_buf_len = dir->length_ + 1 + name_length + 1;
    if (on_stack_buffer_size < total_buf_len) {
      buf = reinterpret_cast<char *>(malloc(total_buf_len));
    } else {
      buf = reinterpret_cast<char *>(on_stack_buf);
    }
    memcpy(buf, dir->name_, dir->length_);
    buf[dir->length_] = '/';
    memcpy(buf + dir->length_ + 1, name, name_length);
    buf[total_buf_len - 1] = '\0';
    const FileName* ret = Get(buf, total_buf_len - 1);
    if (on_stack_buffer_size < total_buf_len) {
      free(buf);
    }
    return ret;
  }
}

}  // namespace firebuild


#endif  // FIREBUILD_FILE_NAME_H_
