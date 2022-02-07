/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_NAME_H_
#define FIREBUILD_FILE_NAME_H_

#include <tsl/hopscotch_map.h>
#include <xxhash.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/platform.h"

namespace firebuild {

struct FileNameHasher;
class FileName {
 public:
  FileName(const FileName& other)
      : name_(reinterpret_cast<const char *>(malloc(other.length_ + 1))),
        length_(other.length_), in_system_location_(other.in_system_location_) {
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
  static bool isDbEmpty();
  static const FileName* Get(const char * const name, ssize_t length,
                             bool force_set_system_location);
  static const FileName* Get(const std::string& name) {
    return Get(name.c_str(), name.size(), false);
  }
  /**
   * Checks if a path semantically begins with one of the given sorted subpaths.
   *
   * Does string operations only, does not look at the file system.
   */
  bool is_at_locations(const std::vector<const FileName *> *locations) const;
  bool is_in_system_location() const {return in_system_location_;}

 private:
  FileName(const char * const name, size_t length, bool copy_name, bool in_system_location)
      : name_(copy_name ? reinterpret_cast<const char *>(malloc(length + 1)) : name),
        length_(length), in_system_location_(in_system_location) {
    if (copy_name) {
      memcpy(const_cast<char*>(name_), name, length);
      const_cast<char*>(name_)[length] = '\0';
    }
  }
  const char * const name_;
  const uint32_t length_;
  const bool in_system_location_;
  static std::unordered_set<FileName, FileNameHasher>* db_;
  static tsl::hopscotch_map<const FileName*, XXH128_hash_t>* hash_db_;
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

extern std::vector<const FileName*>* system_locations;

inline const FileName* FileName::Get(const char * const name, ssize_t length,
                                     bool force_set_system_location = false) {
  FileName tmp_file_name(name, (length == -1) ? strlen(name) : length, false, false);
#ifdef FB_EXTRA_DEBUG
  assert(is_canonical(tmp_file_name.name_, tmp_file_name.length_));
#endif
  auto it = db_->find(tmp_file_name);
  if (it != db_->end()) {
    return &*it;
  } else {
    *const_cast<bool*>(&tmp_file_name.in_system_location_) =
        force_set_system_location ? true : tmp_file_name.is_at_locations(system_locations);
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
