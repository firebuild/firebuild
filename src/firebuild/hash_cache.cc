/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * This implements a global (that is, once per firebuild process)
 * in-memory cache of file hashes.
 *
 * This cache stores the hash of files and directories that are found,
 * along with some metadata that lets determine if the hash needs to be
 * refreshed.
 *
 * Nonexisting files are not cached.
 */

#include "firebuild/hash_cache.h"

namespace firebuild {

/* singleton */
HashCache *hash_cache;

static bool update(const std::string& path, HashCacheEntry *entry, bool force) {
  struct stat64 st;
  if (stat64(path.c_str(), &st) == -1) {
    return false;
  }
  if (!force &&
      st.st_size == entry->size &&
      st.st_mtim.tv_sec == entry->mtime.tv_sec &&
      st.st_mtim.tv_nsec == entry->mtime.tv_nsec &&
      st.st_ino == entry->inode &&
      ((S_ISREG(st.st_mode) && !entry->is_dir) ||
       (S_ISDIR(st.st_mode) && entry->is_dir))) {
    /* Assume contents didn't change. */
    return true;
  }
  /* Update entry, compute hash. */
  entry->size = st.st_size;
  entry->mtime = st.st_mtim;
  entry->inode = st.st_ino;
  return entry->hash.set_from_file(path, &entry->is_dir);
}

HashCacheEntry* HashCache::get_entry(const std::string& path) {
  if (db_.count(path) > 0) {
    HashCacheEntry& entry = db_[path];
    if (!update(path, &entry, false)) {
      db_.erase(path);
      return NULL;
    }
    return &entry;
  } else {
    struct HashCacheEntry new_entry;
    if (!update(path, &new_entry, true)) {
      return NULL;
    }
    db_[path] = new_entry;
    return &db_[path];
  }
}

bool HashCache::get_hash(const std::string& path, Hash *hash, bool *is_dir) {
  HashCacheEntry *entry = get_entry(path);
  if (!entry) {
    return false;
  }
  if (is_dir) {
    *is_dir = entry->is_dir;
  }
  *hash = entry->hash;
  return true;
}

}  // namespace firebuild
