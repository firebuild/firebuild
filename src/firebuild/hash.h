/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_H_
#define FIREBUILD_HASH_H_

#include <sys/stat.h>

#include <cstring>
#include <string>

#include "firebuild/file_name.h"

namespace firebuild {

/**
 * A Hash object represents the binary hash of some blob,
 * and provides methods to compute the hash, and convert to/from
 * an ASCII representation that can be used in filenames.
 *
 * The binary hash is canonical (i.e. architecture independent) version
 * of the XXH128 sum.
 *
 * The ASCII hash is the base64 representation of the binary hash, in
 * ceil(128/6) = 22 characters. The two non-alphanumeric characters of
 * our base64 alphabet are '+' and '^'. No trailing '=' signs to denote
 * the partial block.
 *
 * Command line equivalent:
 * xxh128sum | xxd -r -p | base64 | cut -c1-22 | tr / ^
 */
class Hash {
 public:
  Hash()
      : arr_()
  {}
  explicit Hash(const uint8_t* arr)
      : arr_() {
    memcpy(arr_, arr, sizeof(arr_));
  }

  bool operator==(const Hash& src) const {
    return memcmp(&arr_, &src.arr_, hash_size_) == 0;
  }
  bool operator!=(const Hash& src) const {
    return memcmp(&arr_, &src.arr_, hash_size_) != 0;
  }

  static size_t hash_size() {return hash_size_;}
  /** ASCII representation length without the trailing '\0' */
  static const size_t kAsciiLength = 22;

  void set_from_data(const void *data, ssize_t size);
  bool set_from_fd(int fd, struct stat64 *stat_ptr, bool *is_dir_out);
  bool set_from_file(const FileName *filename, bool *is_dir_out = NULL);

  void set_hash_from_binary(const uint8_t *binary);
  bool set_hash_from_ascii(const std::string &ascii);
  const uint8_t * to_binary() const;
  void to_ascii(char *out) const;
  std::string to_ascii() const {
     char ascii[Hash::kAsciiLength + 1];
     to_ascii(ascii);
     return std::string(ascii);
  }

 private:
  static void decode_block(const char *in, unsigned char *out);
  static void decode_last_block(const char *in, unsigned char *out);
  static void encode_block(const unsigned char *in, char *out);
  static void encode_last_block(const unsigned char *in, char *out);

  static unsigned char encode_map_[64];
  static char decode_map_[256];

  static const unsigned int hash_size_ = 16;
  uint8_t arr_[hash_size_] = {};

  /* This, along with the Hash::hash_maps_initializer_ definition in hash.cc,
   * initializes the encode_map_ and decode_map_ arrays once at startup. */
  class HashMapsInitializer {
   public:
    HashMapsInitializer() {
      int i;
      for (i = 0; i < 26; i++) {
        encode_map_[i] = 'A' + i;
      }
      for (i = 0; i < 26; i++) {
        encode_map_[26 + i] = 'a' + i;
      }
      for (i = 0; i < 10; i++) {
        encode_map_[52 + i] = '0' + i;
      }
      encode_map_[62] = '+';
      encode_map_[63] = '^';
      memset(decode_map_, -1, 256);
      for (int i = 0; i < 64; i++) {
        decode_map_[encode_map_[i]] = i;
      }
    }
  };
  friend class HashMapsInitializer;
  static HashMapsInitializer hash_maps_initializer_;
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Hash& hash, const int level = 0);
std::string d(const Hash *hash, const int level = 0);

}  /* namespace firebuild */
#endif  /* FIREBUILD_HASH_H_ */
