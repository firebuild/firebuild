/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
