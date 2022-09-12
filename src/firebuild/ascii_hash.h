/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_ASCII_HASH_H_
#define FIREBUILD_ASCII_HASH_H_

#include <cassert>
#include <cstring>
#include <string>

#include "firebuild/hash.h"

namespace firebuild {

class AsciiHash {
 public:
  explicit AsciiHash(const char * const str) {
#ifdef FB_EXTRA_DEBUG
    assert(Hash::valid_ascii(str));
#endif
     for (size_t i = 0; i < Hash::kAsciiLength + 1; i++) {
       str_[i] = str[i];
     }
  }
  bool operator<(const AsciiHash& other) const {
    return memcmp(str_, other.str_, Hash::kAsciiLength) < 0;
  }
  const char * c_str() const {
    return str_;
  }
 private:
  char str_[Hash::kAsciiLength + 1];
};

inline std::string d(const AsciiHash& ascii_hash, const int level = 0) {
  return d(ascii_hash.c_str(), level);
}

}  /* namespace firebuild */
#endif  // FIREBUILD_ASCII_HASH_H_
