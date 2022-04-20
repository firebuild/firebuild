/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_CACHE_H_
#define FIREBUILD_HASH_CACHE_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <tsl/hopscotch_map.h>
#include <unistd.h>

#include <string>

#include "firebuild/file_info.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

struct HashCacheEntry {
  FileInfo info {};
  struct timespec mtime {};
  ino_t inode {};  /* skip device, it's unlikely to change */
  bool is_stored {};  /* it's known to be present in the blob cache because we stored it earlier */
};

class HashCache {
 public:
  HashCache() {}
  ~HashCache();
  /**
   * Get some stat information (currently the file type and size) from the cache. This method
   * doesn't compute and doesn't return the hash.
   *
   * @param path         file's path
   * @param[out] is_dir  optionally store if path is a dir
   * @param[out] size    optionally store the size if it's a regular file
   * @return             false if not a regular file or directory
   */
  bool get_statinfo(const FileName* path, bool *is_dir, ssize_t *size);

  /**
   * Get some stat information (currently the file type and size) as well as the hash from the
   * cache.
   *
   * @param path         file's path
   * @param[out] hash    hash to retrive/calculate
   * @param[out] is_dir  optionally store if path is a dir
   * @param[out] size    optionally store the size if it's a regular file
   * @param fd           if >= 0 then read the file from there
   * @param stat_ptr     optionally the file's parameters already stat()'ed
   * @return             false if not a regular file or directory
   */
  bool get_hash(const FileName* path, Hash *hash, bool *is_dir = nullptr,
                ssize_t *size = nullptr, int fd = -1,
                const struct stat64 *stat_ptr = nullptr);

  /**
   * Return the hash of a regular file. Also store this file in the blob cache.
   *
   * @param path       file's path
   * @param[out] hash  hash to retrive/calculate
   * @param fd         if >= 0 then read the file from there
   * @param stat_ptr   optionally the file's parameters already stat()'ed
   * @return           false if not a regular file or directory
   */
  bool store_and_get_hash(const FileName* path, Hash *hash, int fd, const struct stat64 *stat_ptr);

 private:
  tsl::hopscotch_map<const FileName*, HashCacheEntry> db_ = {};

  /**
   * Returns an up-to-date HashCacheEntry corresponding to the given file.
   *
   * It's either of type NOTEXIST if path doesn't correspond to a regular file or directory, or of
   * type ISREG or ISDIR containing some stat information (currently the size in case of ISREG).
   *
   * The hash is also returned if it's cached, but if it wasn't cached then it will not be present
   * in the returned structure, it is not computed by this method.
   *
   * The returned pointer is always non-NULL, readonly, and only valid until the next operation on
   * HashCache.
   *
   * @param path      file's path
   * @param fd        if >= 0 then read the file from there
   * @param stat_ptr  optionally the file's parameters already stat()'ed
   * @return          the requested information about the file
   */
  const HashCacheEntry* get_entry_with_statinfo(const FileName* path, int fd,
                                                const struct stat64 *stat_ptr);

  /**
   * Returns an up-to-date HashCacheEntry corresponding to the given file.
   *
   * It's either of type NOTEXIST if path doesn't correspond to a regular file or directory, or of
   * type ISREG or ISDIR containing some stat information (currently the size in case of ISREG) and
   * the hash.
   *
   * The returned pointer is always non-NULL, readonly, and only valid until the next operation on
   * HashCache.
   *
   * @param path                  file's path
   * @param fd                    if >= 0 then read the file from there
   * @param stat_ptr              optionally the file's parameters already stat()'ed
   * @param store                 whether to store the file in the blob cache
   * @param skip_statinfo_update  assume that the stat info is up-to-date
   * @return                      the requested information about the file
   */
  const HashCacheEntry* get_entry_with_statinfo_and_hash(const FileName* path, int fd,
                                                         const struct stat64 *stat_ptr, bool store,
                                                         bool skip_statinfo_update = false);

  /**
   * A singleton structure representing a file system path that does not point to a regular file or
   * directory. get_entry_...() might return its address.
   */
  static const HashCacheEntry notexist_;

  DISALLOW_COPY_AND_ASSIGN(HashCache);
};

/* singleton */
extern HashCache *hash_cache;

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const HashCacheEntry& hce, const int level = 0);
std::string d(const HashCacheEntry *hce, const int level = 0);

}  /* namespace firebuild */

#endif  // FIREBUILD_HASH_CACHE_H_
