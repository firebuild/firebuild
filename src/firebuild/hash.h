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

#ifndef FIREBUILD_HASH_H_
#define FIREBUILD_HASH_H_

#include <sys/stat.h>

#include <cstring>
#include <string>

#include "firebuild/base64.h"
#include "firebuild/file_name.h"

namespace firebuild {

/**
 * A Hash object represents the binary hash of some blob,
 * and provides methods to compute the hash, and convert to/from
 * an ASCII representation that can be used in filenames.
 *
 * The binary hash is the raw (i.e. architecture dependent XXH128_hash_t)
 * version of the XXH128 sum.
 *
 * The ASCII hash is the base64-like representation of the canonical
 * representation, in ceil(128/6) = 22 characters. The two
 * non-alphanumeric characters of our base64 alphabet are '+' and '^' and
 * none of the characters are at their usual position.
 * The characters are reordered compared to the original base64 mapping.
 * They are ordered by increasing ASCII code to have a set of hash values
 * and their ASCII representation sort the same way.
 * No trailing '=' signs to denote the partial block.
 *
 * Command line equivalent:
 * xxh128sum | xxd -r -p | base64 | cut -c1-22 | tr A-Za-z0-9+/ +0-9A-Z^a-z
 */
class Hash {
 public:
  Hash()
      : hash_()
  {}
  explicit Hash(XXH128_hash_t value)
      : hash_(value)
  {}

  bool operator==(const Hash& other) const {
    return hash_.high64 == other.hash_.high64 && hash_.low64 == other.hash_.low64;
  }
  bool operator!=(const Hash& other) const {
    return hash_.high64 != other.hash_.high64 || hash_.low64 != other.hash_.low64;
  }

  static size_t hash_size() {return hash_size_;}
  /** ASCII representation length without the trailing '\0' */
  static const size_t kAsciiLength {22};

  /**
   * Set the hash from the given buffer.
   */
  void set_from_data(const void *data, ssize_t size);
  /**
   * Set the hash from the given opened file descriptor.
   * The file seek position (read/write offset) is irrelevant.
   *
   * If fd is a directory, its sorted listing is hashed.
   *
   * If stat_ptr is not NULL then it must contain fd's stat data. This can save an fstat() call.
   *
   * @param fd The file descriptor
   * @param stat_ptr Optionally the stat data of fd
   * @param is_dir_out Optionally store here whether fd refers to a directory
   * @param size_out Optionally store the file's size (only if it's a regular file)
   * @return Whether succeeded
   */
  bool set_from_fd(int fd, const struct stat64 *stat_ptr, bool *is_dir_out,
                   off_t *size_out = NULL);
  /**
   * Set the hash from the given file or directory.
   *
   * If a directory is specified, its sorted listing is hashed.
   *
   * @param filename The filename
   * @param stat_ptr Optionally the stat data of fd
   * @param is_dir_out Optionally store here whether filename refers to a directory
   * @param size_out Optionally store the file's size (only if it's a regular file)
   * @return Whether succeeded
   */
  bool set_from_file(const FileName *filename, const struct stat64 *stat_ptr,
                     bool *is_dir_out = NULL, off_t *size_out = NULL);

  /**
   * Sets the hash value directly from the given value.
   * No hash computation takes place.
   */
  void set(XXH128_hash_t);
  XXH128_hash_t get() const { return hash_; }
  const XXH128_hash_t *get_ptr() const { return &hash_; }
  void to_ascii(char *out) const;
  std::string to_ascii() const {
     char ascii[Hash::kAsciiLength + 1];
     to_ascii(ascii);
     return std::string(ascii);
  }
  static bool valid_ascii(const char* const str) {
    return Base64::valid_ascii(str, kAsciiLength);
  }

 private:
  static const unsigned int hash_size_ = sizeof(XXH128_hash_t);
  XXH128_hash_t hash_;
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Hash& hash, const int level = 0);
std::string d(const Hash *hash, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_HASH_H_
