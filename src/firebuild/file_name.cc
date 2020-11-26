/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <cstring>
#include <unordered_set>

#include "firebuild/file_name.h"

namespace firebuild {

std::unordered_set<FileName, FileNameHasher>* FileName::db_;

FileName::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileName, FileNameHasher>();
}

FileName::DbInitializer FileName::db_initializer_;

int FileNamePtrCompare(const FileName * const lhs, const FileName * const rhs) {
  return strcmp(lhs->c_str(), rhs->c_str());
}

}  // namespace firebuild
