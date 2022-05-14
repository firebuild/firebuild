/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * FileInfo describes the (potentially partial) information that we know about a certain file, as it
 * looked like / looks like / will look like in a certain point in time. It's up to the user of this
 * structure to decide which point in time they refer to.
 */

#include "firebuild/file_info.h"

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/hash.h"
#include "firebuild/hash_cache.h"

namespace firebuild {

bool operator==(const FileInfo& lhs, const FileInfo& rhs) {
  return lhs.type_ == rhs.type_ &&
         lhs.size_ == rhs.size_ &&
         lhs.hash_known_ == rhs.hash_known_ &&
         lhs.hash_ == rhs.hash_;
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileInfo& fi, const int level) {
  (void)level;  /* unused */
  return std::string("{FileInfo type=") +
      file_type_to_string(fi.type()) +
      (fi.size_known() ?
          ", size=" + d(fi.size()) : "") +
      (fi.hash_known() ?
          ", hash=" + d(fi.hash()) : "") + "}";
}
std::string d(const FileInfo *fi, const int level) {
  if (fi) {
    return d(*fi, level);
  } else {
    return "{FileInfo NULL}";
  }
}

const char *file_type_to_string(FileType type) {
  switch (type) {
    case DONTKNOW:
      return "dontknow";
    case EXIST:
      return "exist";
    case NOTEXIST:
      return "notexist";
    case NOTEXIST_OR_ISREG_EMPTY:
      return "notexist_or_isreg_empty";
    case NOTEXIST_OR_ISREG:
      return "notexist_or_isreg";
    case ISREG:
      return "isreg";
    case ISDIR:
      return "isdir";
    default:
      assert(0 && "unknown type");
      return "UNKNOWN";
  }
}

}  /* namespace firebuild */
