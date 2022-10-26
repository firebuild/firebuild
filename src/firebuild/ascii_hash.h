/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef FIREBUILD_ASCII_HASH_H_
#define FIREBUILD_ASCII_HASH_H_

#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

#include "firebuild/base64.h"
#include "firebuild/hash.h"

namespace firebuild {

class AsciiHash {
 public:
  AsciiHash() = default;
  explicit AsciiHash(const char * const str) {
#ifdef FB_EXTRA_DEBUG
    assert(Hash::valid_ascii(str));
#endif
     for (size_t i = 0; i < Hash::kAsciiLength + 1; i++) {
       str_[i] = str[i];
     }
  }
  bool operator==(const AsciiHash& other) const {
    return memcmp(str_, other.str_, Hash::kAsciiLength) == 0;
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

namespace std {
template <>
class hash<firebuild::AsciiHash> {
 public:
  size_t operator()(const firebuild::AsciiHash &a) const {
    return XXH3_64bits(a.c_str(), firebuild::Hash::kAsciiLength);
  }
};
}

#endif  // FIREBUILD_ASCII_HASH_H_
