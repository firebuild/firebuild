/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_SUBKEY_H_
#define FIREBUILD_SUBKEY_H_

#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

#include "firebuild/base64.h"
#include "firebuild/hash.h"

namespace firebuild {

class Subkey {
 public:
  Subkey() = default;
  explicit Subkey(const char * const str) {
#ifdef FB_EXTRA_DEBUG
    assert(valid_ascii(str));
#endif
    for (size_t i = 0; i < kAsciiLength + 1; i++) {
      str_[i] = str[i];
    }
    str_[kAsciiLength] = '\0';
  }
  bool operator<(const Subkey& other) const {
    return memcmp(str_, other.str_, kAsciiLength) < 0;
  }
  const char * c_str() const {
    return str_;
  }
  /** ASCII representation length without the trailing '\0' */
  static const size_t kAsciiLength {22};
  static bool valid_ascii(const char* const str) {
    return Base64::valid_ascii(str, kAsciiLength);
  }

 private:
  char str_[kAsciiLength + 1];
};

inline std::string d(const Subkey& ascii_hash, const int level = 0) {
  return d(ascii_hash.c_str(), level);
}

}  /* namespace firebuild */

namespace std {
template <>
class hash<firebuild::Subkey> {
 public:
  size_t operator()(const firebuild::Subkey &a) const {
    return XXH3_64bits(a.c_str(), firebuild::Subkey::kAsciiLength);
  }
};
}

#endif  // FIREBUILD_SUBKEY_H_
