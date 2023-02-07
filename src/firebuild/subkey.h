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

#ifndef FIREBUILD_SUBKEY_H_
#define FIREBUILD_SUBKEY_H_

#ifdef __APPLE__
#include <machine/endian.h>
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#else
#include <endian.h>
#endif

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
  explicit Subkey(uint64_t key) {
    uint64_t be_key = htobe64(key);
    Base64::encode(reinterpret_cast<const unsigned char*>(&be_key), str_, sizeof(uint64_t));
  }
  explicit Subkey(const unsigned char digest[8]) {
    Base64::encode(digest, str_, sizeof(uint64_t));
  }
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
  static const size_t kAsciiLength {11};
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
