/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/hash.h"

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/file_name.h"

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define OFF0  0  /* if a char x[4] is looked at as int32, x[0] is at this bit position */
#define OFF1  8  /*                  ""                   x[1]           ""            */
#define OFF2 16  /*                                                               etc. */
#define OFF3 24
#else
#define OFF0 24
#define OFF1 16
#define OFF2  8
#define OFF3  0
#endif

namespace firebuild  {

unsigned char Hash::encode_map_[];
char Hash::decode_map_[];
Hash::HashMapsInitializer Hash::hash_maps_initializer_;

/**
 * Set the binary hash from the given buffer.
 */
void Hash::set_from_data(const void *data, ssize_t size) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "");

  /* xxhash's doc says:
   * "Streaming functions [...] is slower than single-call functions, due to state management."
   * Let's take the faster path. */
  XXH128_hash_t hash = XXH128(data, size, 0);

  /* Convert from endian-specific representation to endian-independent byte array. */
  XXH128_canonicalFromHash(reinterpret_cast<XXH128_canonical_t *>(&arr_), hash);

  /* Chop off the last 2 bits. They are not converted to base64 for simplicity, and we need to
   * be able to decode as well. */
  arr_[sizeof(arr_) - 1] &= ~0x03;
}

/**
 * Set the binary hash from the given opened file descriptor.
 * The file seek position (read/write offset) is irrelevant.
 *
 * If fd is a directory, its sorted listing is hashed.
 *
 * @param fd The file descriptor
 * @param is_dir_out Optionally store here whether fd refers to a
 * directory
 * @return Whether succeeded
 */
bool Hash::set_from_fd(int fd, bool *is_dir_out) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "fd=%d", fd);

  struct stat64 st;
  if (fstat64(fd, &st) == -1) {
    perror("fstat");
    return false;
  }

  if (S_ISREG(st.st_mode)) {
    /* Compute the hash of a regular file. */
    if (is_dir_out != NULL) {
      *is_dir_out = false;
    }

    void *map_addr;
    if (st.st_size > 0) {
      map_addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
      if (map_addr == MAP_FAILED) {
        FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of regular file: mmap failed");
        return false;
      }
    } else {
      /* Zero length files cannot be mmapped. */
      map_addr = NULL;
    }

    set_from_data(map_addr, st.st_size);

    if (st.st_size > 0) {
      munmap(map_addr, st.st_size);
    }
    return true;

  } else if (S_ISDIR(st.st_mode)) {
    /* Compute the hash of a directory. Its listing is sorted, and
     * concatenated using '\0' as a terminator after each entry. Then
     * this string is hashed. */
    // FIXME place d_type in the string, too?
    if (is_dir_out != NULL) {
      *is_dir_out = true;
    }

    /* Quoting fdopendir(3):
     *   "After a successful call to fdopendir(), fd is used internally by the
     *   implementation, and should not otherwise be used by the application."
     * and closedir(3):
     *   "A successful call to closedir() also closes the underlying file descriptor"
     *
     * It would be an unconventional and hard to use API for this method to close the passed fd.
     * Not calling closedir() on the other hand could leave garbage in the memory, and
     * the caller of this method directly calling close() would also go against the manpage.
     * If we call closedir() and the caller also calls a failing close() then it's prone to
     * raceable errors if one day we go multithreaded or so.
     *
     * So work on a duplicated fd and eventually close that, while keeping the original fd opened.
     */
    DIR *dir = fdopendir(dup(fd));
    if (dir == NULL) {
      FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of directory: fdopendir failed");
      return false;
    }

    std::vector<std::string> listing;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      listing.push_back(entry->d_name);
    }
    closedir(dir);

    std::sort(listing.begin(), listing.end());

    std::string concat;
    for (const auto& entry : listing) {
      concat += entry;
      concat += '\0';
    }
    set_from_data(concat.c_str(), concat.size());
    return true;

  } else {
    FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of special file");
    return false;
  }
}

/**
 * Set the binary hash from the given file or directory.
 *
 * If a directory is specified, its sorted listing is hashed.
 *
 * @param filename The filename
 * @param is_dir_out Optionally store here whether filename refers to a
 * directory
 * @return Whether succeeded
 */
bool Hash::set_from_file(const FileName *filename, bool *is_dir_out) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "filename=%s", D(filename));

  int fd;

  fd = open(filename->c_str(), O_RDONLY);
  if (fd == -1) {
    if (FB_DEBUGGING(FB_DEBUG_HASH)) {
      FB_DEBUG(FB_DEBUG_HASH, "File " + d(filename));
      perror("open");
    }
    return false;
  }

  if (!set_from_fd(fd, is_dir_out)) {
    close(fd);
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_HASH)) {
    FB_DEBUG(FB_DEBUG_HASH, "xxh64sum: " + d(filename) + " => " + d(this));
  }

  close(fd);
  return true;
}

/**
 * Sets the binary hash value directly from the given binary array.
 * No hash computation takes place.
 *
 * Returns true if succeeded, false if the input is not a valid binary hash.
 */
bool Hash::set_hash_from_binary(const uint8_t * const binary) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "");

  if (binary[sizeof(arr_) - 1] & 0x03) {
    return false;
  }
  memcpy(arr_, binary, sizeof(arr_));
  return true;
}

/**
 * Helper method of set_hash_from_ascii().
 *
 * Convert 4 input bytes into 3 output bytes according to base64 decoding.
 *
 * The input value is the input 4 bytes in the machine's byte order, i.e. the numerical 32-bit value
 * differs on little endian vs. big endian machines, but the memory representations are the same.
 */
void Hash::decode_block(uint32_t in, unsigned char *out) {
  uint32_t val =
      (decode_map_[(in >> OFF0) & 0xff] << 18) |
      (decode_map_[(in >> OFF1) & 0xff] << 12) |
      (decode_map_[(in >> OFF2) & 0xff] <<  6) |
      (decode_map_[(in >> OFF3) & 0xff]);
  out[0] = val >> 16;
  out[1] = val >> 8;
  out[2] = val;
}

/**
 * The inverse of to_ascii(): Sets the binary hash value directly from the
 * given ASCII string. No hash computation takes place.
 *
 * Returns true if succeeded, false if the input is not a valid ASCII
 * representation of a hash.
 */
bool Hash::set_hash_from_ascii(const std::string &ascii) {
  if (ascii.size() != ascii_length_) {
    return false;
  }
  for (unsigned int i = 0; i < ascii_length_; i++) {
    if (decode_map_[static_cast<int>(ascii[i])] < 0) {
      return false;
    }
  }

  decode_block(*reinterpret_cast<const uint32_t *>(&ascii[ 0]), arr_);
  decode_block(*reinterpret_cast<const uint32_t *>(&ascii[ 4]), arr_ +  3);
  decode_block(*reinterpret_cast<const uint32_t *>(&ascii[ 8]), arr_ +  6);
  decode_block(*reinterpret_cast<const uint32_t *>(&ascii[12]), arr_ +  9);
  decode_block(*reinterpret_cast<const uint32_t *>(&ascii[16]), arr_ + 12);
  arr_[15] = decode_map_[static_cast<int>(ascii[20])] << 2;

  return true;
}

/**
 * Get the pointer to the raw binary representation.
 */
const uint8_t * Hash::to_binary() const {
  return arr_;
}

/**
 * Helper method of to_ascii().
 *
 * Convert 3 input bytes into 4 output bytes according to base64 encoding.
 *
 * The output value is the output 4 bytes in the machine's byte order, i.e. the numerical 32-bit value
 * differs on little endian vs. big endian machines, but the memory representations are the same.
 */
uint32_t Hash::encode_block(const unsigned char *in) {
  uint32_t val = (static_cast<unsigned int>(in[0]) << 16) |
                 (static_cast<unsigned int>(in[1]) <<  8) |
                 (static_cast<unsigned int>(in[2]));
  return
      (encode_map_[ val >> 18        ] << OFF0) |
      (encode_map_[(val >> 12) & 0x3f] << OFF1) |
      (encode_map_[(val >>  6) & 0x3f] << OFF2) |
      (encode_map_[ val        & 0x3f] << OFF3);
}

/**
 * Get the ASCII representation.
 *
 * See the class's documentation for the exact format.
 */
std::string Hash::to_ascii() const {
  assert(!(arr_[15] & 0x03));

  std::string ret;
  ret.resize(ascii_length_);
  char *ret_c_str = const_cast<char *>(ret.c_str());

  *reinterpret_cast<uint32_t *>(ret_c_str)      = encode_block(arr_);
  *reinterpret_cast<uint32_t *>(ret_c_str +  4) = encode_block(arr_ +  3);
  *reinterpret_cast<uint32_t *>(ret_c_str +  8) = encode_block(arr_ +  6);
  *reinterpret_cast<uint32_t *>(ret_c_str + 12) = encode_block(arr_ +  9);
  *reinterpret_cast<uint32_t *>(ret_c_str + 16) = encode_block(arr_ + 12);
  *(ret_c_str + 20) = encode_map_[arr_[15] >> 2];

  return ret;
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Hash& hash, const int level) {
  (void)level;  /* unused */
  return hash.to_ascii();
}
std::string d(const Hash *hash, const int level) {
  if (hash) {
    return d(*hash, level);
  } else {
    return "{Hash NULL}";
  }
}

}  /* namespace firebuild */
