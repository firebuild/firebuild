/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_CACHE_H_
#define FIREBUILD_HASH_CACHE_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <unordered_map>

#include "firebuild/file_name.h"
#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

struct HashCacheEntry {
  bool is_dir {};
  ssize_t size {};
  struct timespec mtime {};
  ino_t inode {};  /* skip device, it's unlikely to change */
  Hash hash {};
};

class HashCache {
 public:
  HashCache() {}
  ~HashCache();
  /**
   * Calculate hash of a file or directory on the path.
   *
   * If the hash is already present in the cache it is retrieved, unless fd is set to a valid fd.
   * If fd is set, the hash is always recomputed and updated in the cache.
   * @param path     file's path
   * @param[out]     hash to retrive/calculate
   * @param[out]     is_dir path is a dir
   * @param fd       when set to a valid fd the file is read from there
   * @param stat_ptr when fd is set this parameter is set to the fd's stat data
   * @param force    always update the entry
   */
  bool get_hash(const FileName* path, Hash *hash, bool *is_dir = NULL, int fd = -1,
                struct stat64 *stat_ptr = NULL, bool force = false);

 private:
  std::unordered_map<const FileName*, HashCacheEntry> db_ = {};

  /**
   * Calculate hash of a file or directory on the path.
   *
   * If the hash is already present in the cache it is retrieved, unless fd is set to a valid fd.
   * If fd is set, the hash is always recomputed and updated in the cache.
   * @param path     file's path
   * @param[out]     hash to retrive/calculate
   * @param[out]     is_dir path is a dir
   * @param fd when  set to a valid fd the file is read from there
   * @param stat_ptr when fd is set this parameter is set to the fd's stat data
   * @param force    always update the entry
   */
  HashCacheEntry* get_entry(const FileName* path, int fd = -1, struct stat64 *stat_ptr = NULL,
                            bool force = false);

  DISALLOW_COPY_AND_ASSIGN(HashCache);
};

/* singleton */
extern HashCache *hash_cache;

}  // namespace firebuild

#endif  // FIREBUILD_HASH_CACHE_H_
