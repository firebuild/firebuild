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

/**
 * Update the file's hash in the hash_cache if the file changed
 * @param path       the file's path
 * @param fd         if fd != -1 then the file content is read from this file descriptor
 * @param stat_ptr   if set this stat data is used instead of reading the stat data of the path
 * @param[out] entry updated cache entry
 * @param force      always update the entry
 * @return           true on success, false on failure to get and update the file's hash
 */
static bool update(const std::string& path, int fd, struct stat64 *stat_ptr, HashCacheEntry *entry,
                   bool force) {
  struct stat64 st_local, *st;
  st = stat_ptr ? stat_ptr : &st_local;
  if (!stat_ptr && stat64(path.c_str(), st) == -1) {
    return false;
  }
  if (!force &&
      st->st_size == entry->size &&
      st->st_mtim.tv_sec == entry->mtime.tv_sec &&
      st->st_mtim.tv_nsec == entry->mtime.tv_nsec &&
      st->st_ino == entry->inode &&
      ((S_ISREG(st->st_mode) && !entry->is_dir) ||
       (S_ISDIR(st->st_mode) && entry->is_dir))) {
    /* Assume contents didn't change. */
    return true;
  }
  /* Update entry, compute hash. */
  entry->size = st->st_size;
  entry->mtime = st->st_mtim;
  entry->inode = st->st_ino;
  if (fd == -1) {
    return entry->hash.set_from_file(path, &entry->is_dir);
  } else {
    return entry->hash.set_from_fd(fd, &entry->is_dir);
  }
}

HashCacheEntry* HashCache::get_entry(const std::string& path, int fd, struct stat64 *stat_ptr,
                                     bool force) {
  if (db_.count(path) > 0) {
    HashCacheEntry& entry = db_[path];
    if (!update(path, fd, stat_ptr, &entry, force)) {
      db_.erase(path);
      return NULL;
    }
    return &entry;
  } else {
    struct HashCacheEntry new_entry;
    if (!update(path, fd, stat_ptr, &new_entry, true)) {
      return NULL;
    }
    db_[path] = new_entry;
    return &db_[path];
  }
}

bool HashCache::get_hash(const std::string& path, Hash *hash, bool *is_dir, int fd,
                         struct stat64 *stat_ptr, bool force) {
  HashCacheEntry *entry = get_entry(path, fd, stat_ptr, force);
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
