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

#include "firebuild/debug.h"
#include "firebuild/blob_cache.h"
#include "firebuild/file_info.h"
#include "firebuild/file_name.h"

namespace firebuild {

/* singleton */
HashCache *hash_cache;

/**
 * Update the file's hash in the hash_cache if the file changed
 * @param path       the file's path
 * @param fd         if fd != -1 then the file content is read from this file descriptor
 * @param stat_ptr   optionally the file's parameters already stat()'ed
 * @param[out] entry updated cache entry
 * @param store      whether to store the file in the blob cache
 * @param force      always update the entry
 * @return           true on success, false on failure to get and update the file's hash
 */
static bool update(const FileName* path, int fd, const struct stat64 *stat_ptr,
                   HashCacheEntry *entry, bool store, bool force) {
  TRACK(FB_DEBUG_HASH, "path=%s, fd=%d, store=%s, force=%s", D(path), fd, D(store), D(force));

  struct stat64 st_local;
  if (!stat_ptr && (fd >= 0 ? fstat64(fd, &st_local) : stat64(path->c_str(), &st_local)) == -1) {
    return false;
  }
  const struct stat64 *st = stat_ptr ? stat_ptr : &st_local;
  if (!force &&
      (!store || entry->is_stored) &&
      ((S_ISREG(st->st_mode) && entry->info.type() == ISREG) ||
       (S_ISDIR(st->st_mode) && entry->info.type() == ISDIR)) &&
      st->st_size == entry->info.size() &&
      st->st_mtim.tv_sec == entry->mtime.tv_sec &&
      st->st_mtim.tv_nsec == entry->mtime.tv_nsec &&
      st->st_ino == entry->inode) {
    /* Metadata is the same, and don't need to store now in the blob cache.
     * Assume contents didn't change, nothing else to do. */
    return true;
  }

  /* Update entry, compute hash. */
  entry->mtime = st->st_mtim;
  entry->inode = st->st_ino;

  if (store) {
    /* We need to not only remember this entry in this hash cache, but also store the underlying
     * file in the blob cache. So use blob_cache's methods which in turn will compute the hash.
     * The file needs to be a regular file, cannot be a directory. */
    Hash hash;
    bool ret = blob_cache->store_file(path, fd, st, &hash);
    entry->info.set_type(ISREG);
    entry->info.set_size(st->st_size);
    entry->info.set_hash(hash);
    if (ret) {
      entry->is_stored = true;
    }
    return ret;
  } else {
    /* We don't store the file in the blob cache, so just compute the hash directly.
     * The file can be a regular file or a directory (do we use the latter, though??). */
    Hash hash;
    bool is_dir;
    bool ret;
    if (fd == -1) {
      ret = hash.set_from_file(path, &is_dir);
    } else {
      ret = hash.set_from_fd(fd, st, &is_dir);
    }
    if (ret) {
      if (is_dir) {
        entry->info.set_type(ISDIR);
        entry->info.set_hash(hash);
      } else {
        entry->info.set_type(ISREG);
        entry->info.set_size(st->st_size);
        entry->info.set_hash(hash);
      }
    }
    return ret;
  }
}

HashCacheEntry* HashCache::get_entry(const FileName* path, int fd, const struct stat64 *stat_ptr,
                                     bool store) {
  TRACK(FB_DEBUG_HASH, "path=%s, fd=%d, store=%s", D(path), fd, D(store));

  if (db_.count(path) > 0) {
    HashCacheEntry& entry = db_[path];
    if (path->is_in_system_location()) {
      /* System locations are not expected to change. */
      return &entry;
    } else {
      if (!update(path, fd, stat_ptr, &entry, store, false)) {
        db_.erase(path);
        return NULL;
      }
    }
    return &entry;
  } else {
    struct HashCacheEntry new_entry;
    if (!update(path, fd, stat_ptr, &new_entry, store, true)) {
      return NULL;
    }
    db_[path] = new_entry;
    return &db_[path];
  }
}

bool HashCache::get_hash(const FileName* path, Hash *hash, bool *is_dir, ssize_t *size,
                         int fd, const struct stat64 *stat_ptr) {
  TRACK(FB_DEBUG_HASH, "path=%s, fd=%d", D(path), fd);

  HashCacheEntry *entry = get_entry(path, fd, stat_ptr, false);
  if (!entry) {
    return false;
  }
  if (is_dir != nullptr) {
    *is_dir = entry->info.type() == ISDIR;
  }
  if (entry->info.type() != ISDIR && size != nullptr) {
    *size = entry->info.size();
  }
  *hash = entry->info.hash();
  return true;
}

bool HashCache::store_and_get_hash(const FileName* path, Hash *hash,
                                   int fd, const struct stat64 *stat_ptr) {
  TRACK(FB_DEBUG_HASH, "path=%s, fd=%d", D(path), fd);

  HashCacheEntry *entry = get_entry(path, fd, stat_ptr, true);
  if (!entry) {
    return false;
  }
  *hash = entry->info.hash();
  return true;
}

}  /* namespace firebuild */
