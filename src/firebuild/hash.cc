/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/hash.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/hex.h"

namespace firebuild  {

/**
 * Set the hash from the given buffer.
 */
void Hash::set_from_data(const void *data, ssize_t size) {
  // xxhash's doc says:
  // "Streaming functions [...] is slower than single-call functions, due to state management."
  // Let's take the faster path.
  XXH128_hash_t hash = XXH128(data, size, 0);

  // Convert from endian-specific representation to endian-independent byte array.
  XXH128_canonicalFromHash(reinterpret_cast<XXH128_canonical_t *>(&arr_), hash);
}

/**
 * Set the hash from the given protobuf's serialization.
 */
void Hash::set_from_protobuf(const google::protobuf::MessageLite &msg) {
  uint32_t msg_size = msg.ByteSize();
  uint8_t *buf = new uint8_t[msg_size];
  msg.SerializeWithCachedSizesToArray(buf);
  set_from_data(reinterpret_cast<void *>(buf), msg_size);
  delete[] buf;
}

/**
 * Set the hash from the given opened file descriptor.
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
      // Zero length files cannot be mmapped.
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

    DIR *dir = fdopendir(fd);
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
 * Set the hash from the given file or directory.
 *
 * If a directory is specified, its sorted listing is hashed.
 *
 * @param filename The filename
 * @param is_dir_out Optionally store here whether filename refers to a
 * directory
 * @return Whether succeeded
 */
bool Hash::set_from_file(const std::string &filename, bool *is_dir_out) {
  int fd;

  fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    if (FB_DEBUGGING(FB_DEBUG_HASH)) {
      FB_DEBUG(FB_DEBUG_HASH, "File " + filename);
      perror("open");
    }
    return false;
  }

  if (!set_from_fd(fd, is_dir_out)) {
    close(fd);
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_HASH)) {
    FB_DEBUG(FB_DEBUG_HASH, "xxh64sum: " + filename + " => " + this->to_hex());
  }

  close(fd);
  return true;
}

/**
 * The inverse of to_hex(): Sets the binary hash value directly from the
 * given hex string. No hash computation takes place.
 *
 * Returns true if succeeded, false if the input is not a hex string of
 * exactly the required length.
 */
bool Hash::set_hash_from_hex(const std::string &hex) {
  if (hex.size() != sizeof(arr_) * 2) {
    return false;
  }
  if (strspn(hex.c_str(), "0123456789abcdefABCDEF") != sizeof(arr_) * 2) {
    return false;
  }

  std::string part;
  for (unsigned int i = 0; i < sizeof(arr_); i++) {
    part = hex.substr(i * 2, 2);
    arr_[i] = std::stoi(part, NULL, 16);
  }
  return true;
}

/**
 * The inverse of to_binary(): Sets the binary hash value directly from
 * the given binary string. No hash computation takes place.
 *
 * Returns true if succeeded, false if the input is not exactly the
 * required length.
 */
bool Hash::set_hash_from_binary(const std::string &binary) {
  if (binary.size() != sizeof(arr_)) {
    return false;
  }
  memcpy(arr_, binary.c_str(), sizeof(arr_));
  return true;
}

/**
 * Get the raw binary representation, wrapped in std::string for
 * convenience (e.g. easy placement in a protobuf).
 */
std::string Hash::to_binary() const {
  return std::string(arr_, sizeof(arr_));
}

/**
 * Get the lowercase hex representation.
 */

std::string Hash::to_hex() const {
  std::string ret;
  ret.resize(sizeof(arr_) * 2);
  bin2hex(reinterpret_cast<const unsigned char*>(arr_), sizeof(arr_),
          const_cast<char*>(ret.data()));
  return ret;
}

}  // namespace firebuild
