/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include "firebuild/debug.h"

namespace firebuild {

/**
 * Checks if a path semantically begins with the given subpath.
 *
 * Does string operations only, does not look at the file system.
 */
bool path_begins_with(const std::string& path, const std::string& prefix) {
  /* Strip off trailing slashes from prefix. */
  unsigned long prefixlen = prefix.length();
  while (prefixlen > 0 && prefix[prefixlen - 1] == '/') {
    prefixlen--;
  }

  if (path.length() < prefixlen) {
    return false;
  }

  if (memcmp(path.c_str(), prefix.c_str(), prefixlen) != 0) {
    return false;
  }

  if (path.length() == prefixlen) {
    return true;
  }

  if (path.c_str()[prefix.length()] == '/') {
    return true;
  }

  return false;
}

}  // namespace firebuild
