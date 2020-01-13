/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/Hash.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "common/Debug.h"

namespace firebuild  {

bool Hash::update(const std::string &from_path) {
  int fd;
  void *map_addr;

  fd = open(from_path.c_str(), O_RDONLY);
  if (-1 == fd) {
    if (debug_level >= 3) {
      FB_DEBUG(3, "File " + from_path);
      perror("open");
    }
    return false;
  }

  struct stat64 st;
  if (-1 == fstat64(fd, &st)) {
    perror("fstat");
    close(fd);
    return -1;
  } else if (!S_ISREG(st.st_mode)) {
    // Only regular files' hash can be collected
    // TODO(rbalint) debug
    close(fd);
    return false;
  }

  if (st.st_size > 0) {
    map_addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_addr == MAP_FAILED) {
      // TODO debug
      close(fd);
      return false;
    }
  } else {
    // Zero length files cannot be mmapped.
    map_addr = NULL;
  }

  // xxhash's doc says:
  // "Streaming functions [...] is slower than single-call functions, due to state management."
  // Let's take the faster path.
  XXH64_hash_t hash = XXH64(map_addr, st.st_size, 0);

  // Convert from endian-specific representation to endian-independent byte array.
  XXH64_canonicalFromHash((XXH64_canonical_t*)&arr, hash);

  if (firebuild::debug_level >= 2) {
    FB_DEBUG(2, "xxh64sum: " + from_path + " (" + std::to_string(st.st_size) + ") => " + to_string(*this) );
  }

  if (st.st_size > 0) {
    munmap(map_addr, st.st_size);
  }
  close(fd);
  return true;
}

std::string to_string(Hash const &hash) {
  char hex[3];
  std::string ret = "";
  for (int i = 0; i < 8; i++) {
    sprintf(hex, "%02x", hash.arr[i]);
    ret += hex;
  }
  return ret;
}

}  // namespace firebuild
