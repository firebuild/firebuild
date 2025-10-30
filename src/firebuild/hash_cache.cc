/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "firebuild/hash_cache.h"

#include "firebuild/debug.h"
#include "firebuild/blob_cache.h"
#include "firebuild/file_info.h"
#include "firebuild/file_name.h"
#include "firebuild/utils.h"

namespace firebuild {

/* singleton */
HashCache *hash_cache;

/* Update the stat information in the cache. Forget the hash if the stat info changed. */
static bool update_statinfo(const FileName* path, int fd, const struct stat64 *stat_ptr,
                            HashCacheEntry *entry) {
  TRACKX(FB_DEBUG_HASH, 1, 1, HashCacheEntry, entry,
         "path=%s, fd=%d, stat=%s", D(path), fd, D(stat_ptr));

  if (path->is_in_read_only_location() && entry->info.type() != DONTKNOW) {
    /* Assume that for system locations the statinfo never changes. */
    return true;
  }

  if (!path->is_in_read_only_location()) {
    /* For system locations, as per the previous condition, we're updating a brand new record, i.e.
     * type=DONTKNOW. For non-system locations, we're updating a brand new record or an old ISREG or
     * ISDIR type, there's no negative caching for non-system locations so the old type cannot be
     * NOTEXIST. */
    assert(entry->info.type() == DONTKNOW ||
           entry->info.type() == ISREG ||
           entry->info.type() == ISDIR);
  }

  struct stat64 st_local;
  if (!stat_ptr && (fd >= 0 ? fstat64(fd, &st_local) : stat64(path->c_str(), &st_local)) == -1) {
    entry->info.set_type(NOTEXIST);
    entry->is_stored = false;
    return true;
  }
  const struct stat64 *st = stat_ptr ? stat_ptr : &st_local;
  if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)) {
    entry->info.set_type(NOTEXIST);
    entry->is_stored = false;
    return true;
  }

  if (((S_ISREG(st->st_mode) && entry->info.type() == ISREG) ||
       (S_ISDIR(st->st_mode) && entry->info.type() == ISDIR)) &&
      (S_ISDIR(st->st_mode) || st->st_size == entry->info.size()) &&
      st->st_mtim.tv_sec == entry->mtime.tv_sec &&
      st->st_mtim.tv_nsec == entry->mtime.tv_nsec &&
      st->st_ino == entry->inode) {
    /* Metadata is the same. Assume contents didn't change, nothing else to do. */
    return true;
  }

  /* Metadata changed. Update entry, remove hash. */
  entry->mtime = st->st_mtim;
  entry->inode = st->st_ino;
  entry->is_stored = false;
  entry->is_static = false;
  entry->is_static_checked = false;
  if (S_ISREG(st->st_mode)) {
    entry->info.set_type(ISREG);
    entry->info.set_mode_bits(st->st_mode & 07777, 07777 /* we know all the mode bits */);
    entry->info.set_size(st->st_size);
  } else {
    entry->info.set_type(ISDIR);
    entry->info.set_mode_bits(st->st_mode & 07777, 07777 /* we know all the mode bits */);
    entry->info.set_size(-1);
  }
  entry->info.set_hash(nullptr);
  return true;
}

/* Update the hash, maybe assuming that the statinfo is up-to-date. */
static bool update_hash(const FileName* path, int fd, const struct stat64 *stat_ptr,
                        HashCacheEntry *entry, bool store, off_t* stored_bytes,
                        bool skip_statinfo_update) {
  TRACKX(FB_DEBUG_HASH, 1, 1, HashCacheEntry, entry,
         "path=%s, fd=%d, stat=%s, store=%s, skip_statinfo_update=%s",
         D(path), fd, D(stat_ptr), D(store), D(skip_statinfo_update));

  /* This is used by file_info_matches() for a two-phase update, checking in between whether the
   * stat info matches. We want to delay computing the checksum until it's necessary, but we also
   * want to avoid stat()ing the file twice. */
  if (!skip_statinfo_update) {
    update_statinfo(path, fd, stat_ptr, entry);
  }

  /* If there's no such file or directory then there's nothing to hash. */
  if (entry->info.type() == NOTEXIST) {
    return true;
  }

  assert(entry->info.type() == ISREG || entry->info.type() == ISDIR);

  if (store && !entry->is_stored) {
    if (entry->info.type() != ISREG) {
      // FIXME handle if the file type has just changed from regular to something else
      return false;
    }
    /* We need to not only remember this entry in this hash cache, but also store the underlying
     * file in the blob cache. So use blob_cache's methods which in turn will compute the hash.
     * The file needs to be a regular file, cannot be a directory. */
    Hash hash;
    bool ret = blob_cache->store_file(path, 0, fd, 0, entry->info.size(), &hash);
    if (ret) {
      entry->info.set_type(ISREG);
      // FIXME if hash_known_() then we could verify that it didn't change
      entry->info.set_hash(&hash);
      entry->is_stored = true;
      *stored_bytes = entry->info.size();
    }
    return ret;
  } else {
    if (entry->info.hash_known()) {
      /* If the hash is known then it's up-to-date because otherwise update_statinfo() would have
       * cleared it. */
      if (store) {
        /* The entry would be stored if it was not already in the cache. */
        *stored_bytes = entry->info.size();
      }
      return true;
    }
    /* We don't store the file in the blob cache, so just compute the hash directly.
     * The file can be a regular file or a directory. */
    Hash hash;
    bool is_dir;
    bool ret;
    /* In order to save an fstat64() call in set_from_fd(), create a "fake" stat result here. We
     * know that it's a regular file, we know its size, and the rest are irrelevant. */
    struct stat64 st;
    st.st_mode = entry->info.type() == ISREG ? S_IFREG : S_IFDIR;
    st.st_size = entry->info.size();

    if (fd == -1) {
      ret = hash.set_from_file(path, &st, &is_dir);
    } else {
      ret = hash.set_from_fd(fd, &st, &is_dir);
    }
    // FIXME verify that is_dir matches entry->info.type()
    if (ret) {
      entry->info.set_hash(hash);
      if (store) {
        /* The entry would be stored if it was not already in the cache. */
        *stored_bytes = entry->info.size();
      }
    }
    return ret;
  }
}

const HashCacheEntry* HashCache::get_entry_with_statinfo(const FileName* path, int fd,
                                                         const struct stat64 *stat_ptr) {
  TRACK(FB_DEBUG_HASH, "path=%s, fd=%d, stat=%s", D(path), fd, D(stat_ptr));

  if (db_.count(path) > 0) {
    HashCacheEntry& entry = db_[path];
    if (!update_statinfo(path, fd, stat_ptr, &entry)) {
      db_.erase(path);
      return &notexist_;
    }
    if (!path->is_in_read_only_location() && entry.info.type() == NOTEXIST) {
      /* For non-system locations don't store negative entries. */
      db_.erase(path);
      return &notexist_;
    }
    return &entry;
  } else {
    struct HashCacheEntry new_entry {FileInfo(DONTKNOW)};
    if (!update_statinfo(path, fd, stat_ptr, &new_entry)) {
      return &notexist_;
    }
    if (!path->is_in_read_only_location() && new_entry.info.type() == NOTEXIST) {
      /* For non-system locations don't store negative entries. */
      return &notexist_;
    }
    db_[path] = new_entry;
    return &db_[path];
  }
}

const HashCacheEntry* HashCache::get_entry_with_statinfo_and_hash(const FileName* path,
                                                                  int max_writers, int fd,
                                                                  const struct stat64 *stat_ptr,
                                                                  bool store,
                                                                  off_t* stored_bytes,
                                                                  bool skip_statinfo_update) {
  TRACK(FB_DEBUG_HASH, "path=%s, max_writers=%d, fd=%d, stat=%s, store=%s, skip_update=%s",
      D(path), max_writers, fd, D(stat_ptr), D(store), D(skip_statinfo_update));

  if (path->writers_count() > max_writers) {
    /* The file could be written while calculating the hash, don't take that risk. */
    return &dontknow_;
  }

  if (db_.count(path) > 0) {
    HashCacheEntry& entry = db_[path];
    if (!update_hash(path, fd, stat_ptr, &entry, store, stored_bytes, skip_statinfo_update)) {
      db_.erase(path);
      return &notexist_;
    }
    if (!path->is_in_read_only_location() && entry.info.type() == NOTEXIST) {
      /* For non-system locations don't store negative entries. */
      db_.erase(path);
      return &notexist_;
    }
    return &entry;
  } else {
    struct HashCacheEntry new_entry {FileInfo(DONTKNOW)};
    if (!update_hash(path, fd, stat_ptr, &new_entry, store, stored_bytes, skip_statinfo_update)) {
      return &notexist_;
    }
    if (!path->is_in_read_only_location() && new_entry.info.type() == NOTEXIST) {
      /* For non-system locations don't store negative entries. */
      return &notexist_;
    }
    db_[path] = new_entry;
    return &db_[path];
  }
}

bool HashCache::get_statinfo(const FileName* path, bool *is_dir, ssize_t *size) {
  TRACK(FB_DEBUG_HASH, "path=%s", D(path));

  if (path->is_in_ignore_location()) {
    return false;
  } else if (path->is_in_read_only_location()) {
    /* For system files go through our cache, as if we were interested in the hash too. */
    const HashCacheEntry *entry = get_entry_with_statinfo(path, -1, nullptr);
    if (entry->info.type() == NOTEXIST) {
      return false;
    }
    if (is_dir) {
      *is_dir = entry->info.type() == ISDIR;
    }
    if (size && entry->info.type() != ISDIR) {
      *size = entry->info.size();
    }
    return true;
  } else {
    /* For non-system files just stat() the file, completely bypassing the cache. Looking up and
     * updating the cache entry would just be a waste of CPU time since next time (when we do care
     * about the checksum) we'll have to update it anyway. */
    struct stat64 st;
    if (stat64(path->c_str(), &st) == -1 ||
        (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
      return false;
    }
    if (is_dir) {
      *is_dir = S_ISDIR(st.st_mode);
    }
    if (size && !S_ISDIR(st.st_mode)) {
      *size = st.st_size;
    }
    return true;
  }
}

bool HashCache::get_hash(const FileName* path, int max_writers, Hash *hash, bool *is_dir,
                         ssize_t *size, int fd, const struct stat64 *stat_ptr) {
  TRACK(FB_DEBUG_HASH, "path=%s, max_writers=%d, fd=%d, stat=%s",
      D(path), max_writers, fd, D(stat_ptr));

  if (path->is_in_ignore_location()) {
    return false;
  }
  const HashCacheEntry *entry = get_entry_with_statinfo_and_hash(path, max_writers, fd, stat_ptr,
                                                                 false, nullptr);
  if (entry->info.type() == NOTEXIST || entry->info.type() == DONTKNOW) {
    return false;
  }
  if (is_dir) {
    *is_dir = entry->info.type() == ISDIR;
  }
  if (entry->info.type() != ISDIR && size) {
    *size = entry->info.size();
  }
  *hash = entry->info.hash();
  return true;
}

#ifndef __APPLE__
bool HashCache::get_is_static(const FileName* path, bool *is_static) {
  TRACK(FB_DEBUG_HASH, "path=%s",
      D(path));

  if (!path || path->is_in_ignore_location()) {
    return false;
  }
  const HashCacheEntry *entry = get_entry_with_statinfo(path, -1, nullptr);
  if (entry->info.type() == NOTEXIST || entry->info.type() == DONTKNOW) {
    return false;
  }

  if (!entry->is_static_checked) {
    bool static_linked = is_statically_linked(path->c_str());
    const_cast<HashCacheEntry*>(entry)->is_static = static_linked;
    const_cast<HashCacheEntry*>(entry)->is_static_checked = true;
  }

  *is_static = entry->is_static;
  return true;
}
#endif

bool HashCache::store_and_get_hash(const FileName* path, int max_writers, Hash *hash,
                                   off_t* stored_bytes, int fd, const struct stat64 *stat_ptr) {
  TRACK(FB_DEBUG_HASH, "path=%s, max_writers=%d, fd=%d, stat=%s",
      D(path), max_writers, fd, D(stat_ptr));

  if (path->is_in_ignore_location()) {
    return false;
  }
  const HashCacheEntry *entry = get_entry_with_statinfo_and_hash(path, max_writers, fd, stat_ptr,
                                                                 true, stored_bytes);
  if (!entry) {
    return false;
  }
  *hash = entry->info.hash();
  return true;
}

bool HashCache::file_info_matches(const FileName *path, const FileInfo& query) {
  TRACK(FB_DEBUG_HASH, "path=%s, query=%s", D(path), D(query));

  if (path->is_in_ignore_location()) {
    /* Information about files in the ignore locations should not be stored in the cache.
     * Return false to not use this cache entry, while we could return true, because we should
     * not care. */
    return false;
  }
  const HashCacheEntry *entry = get_entry_with_statinfo(path, -1, nullptr);

  /* We do have an up-to-date stat information now. Check if the query matches it. */
  switch (query.type()) {
    case DONTKNOW:
      assert(0 && "shouldn't query the HashCache to see if <no information> matches");
      return true;
    case EXIST:
      if (entry->info.type() == NOTEXIST) {
        return false;
      }
      break;
    case NOTEXIST:
      return (entry->info.type() == NOTEXIST);
    case NOTEXIST_OR_ISREG:
      if (entry->info.type() == NOTEXIST) {
        return true;
      } else if (entry->info.type() == ISREG) {
        if (query.size() >= 0 && query.size() != entry->info.size()) {
          return false;
        }
      } else {
        return false;
      }
      break;
    case ISREG:
      if (entry->info.type() != ISREG) {
        return false;
      }
      if (query.size() >= 0 && query.size() != entry->info.size()) {
        return false;
      }
      break;
    case ISDIR:
      if (entry->info.type() != ISDIR) {
        return false;
      }
      break;
  }

  if ((query.mode() & query.mode_mask()) != (entry->info.mode() & query.mode_mask())) {
    return false;
  }

  /* Everything matches so far. If the query doesn't contain a hash then it's a match. */
  if (!query.hash_known()) {
    return true;
  }

  assert(query.type() == ISREG || query.type() == ISDIR || query.type() == NOTEXIST_OR_ISREG);
  assert((query.type() == NOTEXIST_OR_ISREG && entry->info.type() == ISREG)
         || entry->info.type() == query.type());

  /* We need to compare the hash. The current cache entry does not necessarily contain this
   * information, because it's expensive to compute it so we defer it as long as possible. But if
   * the entry already contains it then save some time by not looking it up in the cache again. */
  if (!entry->info.hash_known()) {
    entry = get_entry_with_statinfo_and_hash(path, 0, -1, nullptr, false, nullptr,
                                             true /* don't stat again */);

    if ((entry->info.type() != ISREG && entry->info.type() != ISDIR)
        || !entry->info.hash_known()) {
      /* Could not get the hash possibly because the file/directory is open for writing. */
      return false;
    }
  }

  return entry->info.hash() == query.hash();
}

const FileName* HashCache::resolve_command(const char* cmd, size_t cmd_len,
                                          const char* path, size_t path_len, const FileName* wd) {
  TRACK(FB_DEBUG_PROC, "cmd=%s, path=%s", D(cmd), D(path));

  size_t candidate_buf_size = path_len + 1 + cmd_len + 1;
  char* candidate_buf = static_cast<char*>(alloca(candidate_buf_size));
  size_t candidate_len = 0;
  assert(!path_is_absolute(cmd));
  if (cmd_len == 0 || !path) {
    /* Not a command to resolve on PATH or PATH is null. */
    return nullptr;
  }

  const char *cursor = path;
  while (true) {
    const char *colon = strchr(cursor, ':');
    if (colon == nullptr) {
      candidate_len = path_len - (cursor - path);
    } else {
      candidate_len = colon - cursor;
    }
    memcpy(candidate_buf, cursor, candidate_len);
    candidate_buf[candidate_len] = '\0';
    // TODO(rbalint) this is not very efficient, consider caching canonical paths
    // luckily this is not called that often
    if (!is_canonical(candidate_buf, candidate_len)) {
      size_t new_len = make_canonical(candidate_buf, candidate_len);
      candidate_len = new_len;
    }
    candidate_buf[candidate_len] = '/';
    memcpy(candidate_buf + candidate_len + 1, cmd, cmd_len);
    candidate_buf[candidate_len + 1 + cmd_len] = '\0';
    candidate_len += 1 + cmd_len;

    if (!path_is_absolute(candidate_buf)) {
      /* Make it absolute based on the working directory. */
      if (!wd) {
        /* Cannot resolve relative path without a working directory. */
        if (colon == nullptr) {
          break;
        } else {
          cursor = colon + 1;
          continue;
        }
      }
      const size_t wd_len = wd->length();
      const size_t new_candidate_len = wd_len + 1 + candidate_len;
      if (new_candidate_len + 1 > candidate_buf_size) {
        char* candidate_buf2 = static_cast<char*>(alloca(new_candidate_len + 1));
        memcpy(candidate_buf2, candidate_buf, candidate_len + 1);
        candidate_buf = candidate_buf2;
        /* candidate_buf_size is not valid anymore, but not used either. */
      }
      memmove(candidate_buf + wd_len + 1, candidate_buf, candidate_len + 1);
      memcpy(candidate_buf, wd->c_str(), wd_len);
      candidate_buf[wd_len] = '/';
      candidate_len += wd_len + 1;
    }
    const FileName* candidate = FileName::Get(candidate_buf, candidate_len);
    bool is_directory = false;
    if (this->get_statinfo(candidate, &is_directory, nullptr)) {
      if (!is_directory) {
        return candidate;
      }
    }
    if (colon == nullptr) {
      break;
    }
    cursor = colon + 1;
  }
  return nullptr;
}

const HashCacheEntry HashCache::notexist_ {FileInfo(NOTEXIST)};
const HashCacheEntry HashCache::dontknow_ {FileInfo(DONTKNOW)};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const HashCacheEntry& hce, const int level) {
  (void)level;  /* unused */

  return std::string("{HashCacheEntry info=") + d(hce.info) +
      ", mtime={" + d(hce.mtime.tv_sec) + "," + d(hce.mtime.tv_nsec) + "}" +
      ", inode=" + d(hce.inode) +
      ", is_stored=" + d(hce.is_stored) + "}";
}
std::string d(const HashCacheEntry *hce, const int level) {
  if (hce) {
    return d(*hce, level);
  } else {
    return "{HashCacheEntry NULL}";
  }
}

}  /* namespace firebuild */
