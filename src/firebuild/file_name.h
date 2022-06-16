/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_NAME_H_
#define FIREBUILD_FILE_NAME_H_

// Workaround for https://github.com/Tessil/hopscotch-map/issues/55
#ifndef __clang__
#pragma GCC optimize ("-fno-strict-aliasing")   // NOLINT(whitespace/parens)
#endif
#include <tsl/hopscotch_map.h>
#ifndef __clang__
#pragma GCC reset_options
#endif
#include <xxhash.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/debug.h"

namespace firebuild {

class Process;

typedef uint32_t file_generation_t;

struct FileNameHasher;
class FileName {
 public:
  FileName(const FileName& other)
      : name_(reinterpret_cast<const char *>(malloc(other.length_ + 1))),
        length_(other.length_), in_ignore_location_(other.in_ignore_location_),
        in_system_location_(other.in_system_location_) {
    memcpy(const_cast<char*>(name_), other.name_, other.length_ + 1);
  }
  const char * c_str() const {return name_;}
  std::string to_string() const {return std::string(name_);}
  uint32_t length() const {return length_;}
  size_t hash() const {return XXH3_64bits(name_, length_);}
  const XXH128_hash_t& hash_XXH128() const {
    auto it = hash_db_->find(this);
    if (it != hash_db_->end()) {
      return it->second;
    } else {
      /* Not found, add a copy to the set. */
      return (hash_db_->insert({this,  XXH3_128bits(name_, length_)}).first)->second;
    }
  }
  bool is_open_for_writing() const {
    /* Files in ignored locations should not even be queried. */
    assert(!is_in_ignore_location());
    auto it = write_fds_db_->find(this);
    if (it != write_fds_db_->end()) {
      assert(it->second.first > 0);
      return true;
    } else {
      return false;
    }
  }
  void open_for_writing(Process* proc) const;
  void close_for_writing() const {
    if (is_in_ignore_location()) {
      /* Ignored locations can be ignored here, too. */
      return;
    }
    auto it = write_fds_db_->find(this);
    assert(it != write_fds_db_->end());
    assert(it->second.first > 0);
    if (it->second.first > 1) {
      it.value().first--;
    } else {
      write_fds_db_->erase(it);
    }
  }

  file_generation_t generation() const {
    auto it = generation_db_->find(this);
    if (it != generation_db_->end()) {
      assert(it->second > 0);
      return it->second;
    } else {
      return 0;
    }
  }
  static bool isDbEmpty();
  static const FileName* Get(const char * const name, ssize_t length);
  static const FileName* Get(const std::string& name) {
    return Get(name.c_str(), name.size());
  }
  static const FileName* GetParentDir(const char * const name, ssize_t length);

  bool is_in_ignore_location() const {return in_ignore_location_;}
  bool is_in_system_location() const {return in_system_location_;}

  std::string without_dirs() const {
    // TODO(rbalint) use std::string::ends_with when we switch to c++20
    const char* last_slash = strrchr(name_, '/');
    if (last_slash) {
      return std::string(last_slash + 1);
    } else {
      return to_string();
    }
  }
  static const FileName* default_tmpdir;

 private:
  FileName(const char * const name, size_t length, bool copy_name)
      : name_(copy_name ? reinterpret_cast<const char *>(malloc(length + 1)) : name),
        length_(length), in_ignore_location_(false), in_system_location_(false) {
    if (copy_name) {
      memcpy(const_cast<char*>(name_), name, length);
      const_cast<char*>(name_)[length] = '\0';
    }
  }

  /**
   * Checks if a path semantically begins with one of the given sorted subpaths.
   *
   * Does string operations only, does not look at the file system.
   */
  bool is_at_locations(const std::vector<std::string> *locations) const;

  const char * const name_;
  const uint32_t length_;
  const bool in_ignore_location_;
  const bool in_system_location_;
  static std::unordered_set<FileName, FileNameHasher>* db_;
  static tsl::hopscotch_map<const FileName*, XXH128_hash_t>* hash_db_;
  /** Number of FileFDs open for writing referencing this file. */
  static tsl::hopscotch_map<const FileName*, std::pair<int, Process*>>* write_fds_db_;
  /**
   * A generation of the file is when it is kept open by a set of writers.
   * Whenever all writers close the file and thus the refcount in write_fds_db_ decreases to zero
   * the generation is closed, but the generation number stays the same. When the new writer opens
   * the file a new generation is opened.
   * A file's generation number is 0 until it is opened for writing for the first time.
   */
  static tsl::hopscotch_map<const FileName*, file_generation_t>* generation_db_;

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
  return lhs.length() == rhs.length() && memcmp(lhs.c_str(), rhs.c_str(), lhs.length()) == 0;
}

struct FileNameHasher {
  std::size_t operator()(const FileName& s) const noexcept {
    return s.hash();
  }
};

/** Helper struct for std::sort */
struct FileNameLess {
  bool operator()(const FileName* f1, const FileName* f2) const {
    return strcmp(f1->c_str(), f2->c_str()) < 0;
  }
};

extern std::vector<std::string> *ignore_locations;
extern std::vector<std::string> *system_locations;

inline const FileName* FileName::Get(const char * const name, ssize_t length) {
  FileName tmp_file_name(name, (length == -1) ? strlen(name) : length, false);
#ifdef FB_EXTRA_DEBUG
  assert(is_canonical(tmp_file_name.name_, tmp_file_name.length_));
#endif
  auto it = db_->find(tmp_file_name);
  if (it != db_->end()) {
    return &*it;
  } else {
    *const_cast<bool*>(&tmp_file_name.in_ignore_location_) =
        tmp_file_name.is_at_locations(ignore_locations);
    *const_cast<bool*>(&tmp_file_name.in_system_location_) =
        tmp_file_name.is_at_locations(system_locations);
    /* Not found, add a copy to the set. */
    return &*db_->insert(tmp_file_name).first;
  }
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileName& fn, const int level = 0);
std::string d(const FileName *fn, const int level = 0);

}  /* namespace firebuild */

#endif  // FIREBUILD_FILE_NAME_H_
