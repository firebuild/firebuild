/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <tsl/hopscotch_map.h>

#include <cstring>
#include <unordered_set>
#include <vector>

#include "firebuild/file_name.h"

namespace firebuild {

std::unordered_set<FileName, FileNameHasher>* FileName::db_;
tsl::hopscotch_map<const FileName*, XXH128_hash_t>* FileName::hash_db_;
tsl::hopscotch_map<const FileName*, int>* FileName::write_fds_db_;

FileName::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileName, FileNameHasher>();
  hash_db_ = new tsl::hopscotch_map<const FileName*, XXH128_hash_t>();
  write_fds_db_ = new tsl::hopscotch_map<const FileName*, int>();
}

bool FileName::isDbEmpty() {
  return !db_ || db_->empty();
}

FileName::DbInitializer FileName::db_initializer_;

/**
 * Return parent dir or nullptr for "/"
 */
const FileName* FileName::GetParentDir(const char * const name, ssize_t length) {
  /* name is canonicalized, so just simply strip the last component */
  ssize_t slash_pos = length - 1;
  for (; slash_pos >= 0; slash_pos--) {
    if (name[slash_pos] == '/') {
      break;
    }
  }

  /* A path that does not have a '/' in it or "/" itself does not have a parent */
  if (slash_pos == -1 || length == 1) {
    return nullptr;
  }

  if (slash_pos == 0) {
    /* Path is in the "/" dir. */
    return Get("/", 0);
  } else {
    char* parent_name = reinterpret_cast<char*>(alloca(slash_pos + 1));
    memcpy(parent_name, name, slash_pos);
    parent_name[slash_pos] = '\0';
    return Get(parent_name, slash_pos);
  }
}

/**
 * Checks if a path semantically begins with one of the given sorted subpaths.
 *
 * Does string operations only, does not look at the file system.
 */
bool FileName::is_at_locations(const std::vector<std::string> *locations) const {
  for (const std::string& location : *locations) {
    const char *location_name = location.c_str();
    size_t location_len = location.length();
    while (location_len > 0 && location_name[location_len - 1] == '/') {
      location_len--;
    }

    if (this->length_ < location_len) {
      continue;
    }

    if (this->name_[location_len] != '/' && this->length_ > location_len) {
      continue;
    }

    /* Try comparing only the first 8 bytes to potentially save a call to memcmp */
    if (location_len >= sizeof(int64_t)
        && (*reinterpret_cast<const int64_t*>(location_name)
            != *reinterpret_cast<const int64_t*>(this->name_))) {
      /* Does not break the loop if this->name_ > location->name_ */
      // TODO(rbalint) maybe the loop could be broken making this function even faster
      continue;
    }

    const int memcmp_res = memcmp(location_name, this->name_, location_len);
    if (memcmp_res < 0) {
      continue;
    } else if (memcmp_res > 0) {
      return false;
    }

    if (this->length_ == location_len) {
      return true;
    }

    if (this->name_[location_len] == '/') {
      return true;
    }
  }
  return false;
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileName& fn, const int level) {
  (void)level;  /* unused */
  return d(fn.to_string());
}
std::string d(const FileName *fn, const int level) {
  if (fn) {
    return d(*fn, level);
  } else {
    return "{FileName NULL}";
  }
}

}  /* namespace firebuild */
