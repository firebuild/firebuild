/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_CACHE_H_
#define FIREBUILD_HASH_CACHE_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <unordered_map>

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

  bool get_hash(const std::string& path, Hash *hash, bool *is_dir = NULL);

 private:
  std::unordered_map<std::string, HashCacheEntry> db_ = {};

  HashCacheEntry* get_entry(const std::string& path);

  DISALLOW_COPY_AND_ASSIGN(HashCache);
};

/* singleton */
extern HashCache *hash_cache;

}  // namespace firebuild

#endif  // FIREBUILD_HASH_CACHE_H_
