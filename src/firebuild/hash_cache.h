/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_CACHE_H_
#define FIREBUILD_HASH_CACHE_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <tsl/hopscotch_map.h>
#include <unistd.h>

#include <string>

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
  bool is_stored {};  /* it's known to be present in the blob cache because we stored it earlier */
};

class HashCache {
 public:
  HashCache() {}
  ~HashCache();
  /**
   * Calculate hash of a file or directory on the path, and update the hash cache.
   *
   * If the file with this name and the same metadata (size, timestamp etc.) is already cached
   * then return the hash from the cache.
   * @param path     file's path
   * @param[out]     hash to retrive/calculate
   * @param[out]     is_dir path is a dir
   * @param fd       when set to a valid fd the file is read from there
   * @param stat_ptr when fd is set this parameter is set to the fd's stat data
   */
  bool get_hash(const FileName* path, Hash *hash, bool *is_dir = NULL, int fd = -1,
                struct stat64 *stat_ptr = NULL);

  /**
   * Calculate hash of a regular file on the path, and update the hash cache.
   * Also store this file in the blob cache.
   *
   * If the file with this name and the same metadata (size, timestamp etc.) is already cached
   * and the contents are already in the blob cache then return the hash from the hash cache.
   * @param path     file's path
   * @param[out]     hash to retrive/calculate
   * @param fd       the file is read from there
   * @param stat_ptr when fd is set this parameter is set to the fd's stat data
   */
  bool store_and_get_hash(const FileName* path, Hash *hash, int fd, struct stat64 *stat_ptr);

 private:
  tsl::hopscotch_map<const FileName*, HashCacheEntry> db_ = {};

  /**
   * Calculate hash of a file or directory on the path, and update the hash cache.
   *
   * If the file with this name and the same metadata (size, timestamp etc.) is already cached
   * then return the hash from the cache.
   * @param path     file's path
   * @param[out]     hash to retrive/calculate
   * @param[out]     is_dir path is a dir
   * @param fd when  set to a valid fd the file is read from there
   * @param stat_ptr when fd is set this parameter is set to the fd's stat data
   * @param store    whether to store the file in the blob cache
   */
  HashCacheEntry* get_entry(const FileName* path, int fd = -1, struct stat64 *stat_ptr = NULL,
                            bool store = false);

  DISALLOW_COPY_AND_ASSIGN(HashCache);
};

/* singleton */
extern HashCache *hash_cache;

}  // namespace firebuild

#endif  // FIREBUILD_HASH_CACHE_H_
