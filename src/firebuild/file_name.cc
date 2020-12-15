/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <cstring>
#include <unordered_set>
#include <vector>

#include "firebuild/file_name.h"

namespace firebuild {

std::unordered_set<FileName, FileNameHasher>* FileName::db_;

FileName::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileName, FileNameHasher>();
}

FileName::DbInitializer FileName::db_initializer_;

/**
 * Checks if a path semantically begins with the given subpath.
 *
 * Does string operations only, does not look at the file system.
 */
bool FileName::is_at_locations(const std::vector<const FileName *> *locations) const {
  for (const auto location : *locations) {
    size_t location_len = location->length();
    while (location_len > 0 && location->name_[location_len - 1] == '/') {
      location_len--;
    }

    if (this->length_ < location_len) {
      continue;
    }

    if (memcmp(location->name_, this->name_, location_len) != 0) {
      continue;
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

int FileNamePtrPtrCompare(const FileName * const * const lhs, const FileName * const * const rhs) {
  return strcmp((*lhs)->c_str(), (*rhs)->c_str());
}

}  // namespace firebuild
