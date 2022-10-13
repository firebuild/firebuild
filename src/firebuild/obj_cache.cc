/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/*
 * obj-cache is a weird caching structure where a key can contain
 * multiple values. More precisely, a key contains a list of subkeys,
 * and a (key, subkey) pair points to a value.
 *
 * In practice, one ProcessFingerprint can have multiple
 * ProcessInputsOutputs associated with it. The key is the hash of
 * ProcessFingerprint's serialization. The subkey happens to be the hash
 * of ProcessInputsOutputs's serialization, although it could easily be
 * anything else.
 *
 * Currently the backend is the filesystem. The multiple values are
 * stored as separate file of a given directory. The list of subkeys is
 * retrieved by listing the directory.
 *
 * E.g. ProcessFingerprint1's hash in ASCII is "fingerprint1". Underneath
 * it there are two values: ProcessInputsOutputs1's hash in ASCII is
 * "inputsoutputs1",ProcessInputsOutputs2's hash in ASCII is
 * "inputsoutputs2". The directory structure is:
 * - f/fi/fingerprint1/inputsoutputs1
 * - f/fi/fingerprint1/inputsoutputs2
 */

#include "firebuild/obj_cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <tsl/hopscotch_set.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "firebuild/ascii_hash.h"
#include "firebuild/config.h"
#include "firebuild/debug.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/hash.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"
#include "firebuild/utils.h"

namespace firebuild {

/* singleton */
ObjCache *obj_cache;

ObjCache::ObjCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}


/* /x/xx/<ascii key>/<ascii subkey> */
static size_t kObjCachePathLength = 1 + 1 + 1 + 2 + 1 + 2 * Hash::kAsciiLength + 1;

/*
 * Constructs the directory name where the cached files are to be
 * stored, or read from. Optionally creates the necessary subdirectories
 * within the cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key", and
 * create_dirs=true, it creates the directories "base/k", "base/k/ke"
 * and "base/k/ke/key" and returns the latter.
 */
static void construct_cached_dir_name(const std::string &base, const Hash &key,
                                       bool create_dirs, char* path) {
  char ascii[Hash::kAsciiLength + 1];
  key.to_ascii(ascii);
  char *end = path;
  memcpy(end, base.c_str(), base.length());
  end += base.length();
  *end++ = '/'; *end++ = ascii[0];
  if (create_dirs) {
    *end = '\0';
    mkdir(path, 0700);
  }
  *end++ = '/'; *end++ = ascii[0]; *end++ = ascii[1];
  if (create_dirs) {
    *end = '\0';
    mkdir(path, 0700);
  }
  *end++ = '/';
  memcpy(end, ascii, sizeof(ascii));
  if (create_dirs) {
    mkdir(path, 0700);
  }
}

/*
 * Constructs the filename where the cached file is to be stored, or
 * read from. Optionally creates the necessary subdirectories within the
 * cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key",
 * subkey's ASCII representation being "subkey", and create_dirs=true, it
 * creates the directories "base/k", "base/k/ke" and "base/k/ke/key" and
 * returns "base/k/ke/key/subkey".
 */
static void construct_cached_file_name(const std::string &base,
                                       const Hash &key,
                                       const char* const subkey,
                                       bool create_dirs,
                                       char* path) {
  construct_cached_dir_name(base, key, create_dirs, path);
  path[base.length() + kObjCachePathLength - Hash::kAsciiLength - 1] = '/';
  memcpy(&path[base.length() + kObjCachePathLength - Hash::kAsciiLength], subkey,
         Hash::kAsciiLength + 1);
}


/**
 * Store a serialized entry in obj-cache.
 *
 * @param key The key
 * @param entry The entry to serialize and store
 * @param debug_key Optionally the key as pb for debugging purposes
 * @return Whether succeeded
 */
bool ObjCache::store(const Hash &key,
                     const FBBSTORE_Builder * const entry,
                     const FBBFP_Serialized * const debug_key) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: storing entry, key " + d(key));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE) && debug_key) {
    /* Place a human-readable version of the key in the cache, for easier debugging. */
    char* path_debug =
        reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength
                                       - Hash::kAsciiLength + 1 +strlen(kDirDebugJson) + 1));
    construct_cached_dir_name(base_dir_, key, true, path_debug);
    path_debug[base_dir_.length() + kObjCachePathLength - Hash::kAsciiLength - 1] = '/';
    memcpy(&path_debug[base_dir_.length() + kObjCachePathLength - Hash::kAsciiLength],
           kDirDebugJson, strlen(kDirDebugJson) + 1);

    FILE *f = fopen(path_debug, "w");
    debug_key->debug(f);
    fclose(f);
  }

  const char* tmpfile_end = "/new.XXXXXX";
  char* tmpfile = static_cast<char*>(malloc(base_dir_.length() + strlen(tmpfile_end) + 1));
  memcpy(tmpfile, base_dir_.c_str(), base_dir_.length());
  memcpy(tmpfile + base_dir_.length(), tmpfile_end, strlen(tmpfile_end) + 1);

  int fd_dst = mkstemp(tmpfile);  /* opens with O_RDWR */
  if (fd_dst == -1) {
    fb_perror("Failed mkstemp() for storing cache object");
    assert(0);
    free(tmpfile);
    return false;
  }

  // FIXME Do we need to split large files into smaller writes?
  // FIXME add basic error handling
  // FIXME Is it faster if we alloca() for small sizes instead of malloc()?
  // FIXME Is it faster to ftruncate() the file to the desired size, then mmap,
  // then serialize to the mapped memory, then ftruncate() again to the actual size?
  size_t len = entry->measure();
  char *entry_serial = reinterpret_cast<char *>(malloc(len));
  entry->serialize(entry_serial);
  fb_write(fd_dst, entry_serial, len);
  close(fd_dst);

  /* Create randomized object file */
  char* path_dst = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  // TODO(rbalint) user shorter subkey and use different alphabet to
  // make it more apparent that the subkey is not a hash of anything
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  /* XXH128_hash_t stores high and low 64 bits in little endian order.
   * Store both seconds and nanoseconds in the highest 64 bits to have the non-zero bits
   * closer to the beginning. The seconds since the epoch is stored in the first 34 bits
   * that will be enough until 2514 and the nanoseconds are stored in the next 30. */
  Hash subkey({0, (static_cast<uint64_t>(time.tv_sec) << 30) +
      static_cast<uint64_t>(time.tv_nsec)});
  if (FB_DEBUGGING(FB_DEBUG_DETERMINISTIC_CACHE)) {
    /* Debugging: Instead of a randomized filename (which is fast to generate) use the content's
     * hash for a deterministic filename. */
    subkey.set_from_data(entry_serial, len);
  }

  construct_cached_file_name(base_dir_, key, subkey.to_ascii().c_str(), true, path_dst);
  free(entry_serial);

  if (rename(tmpfile, path_dst) == -1) {
    fb_perror("Failed rename() while storing cache object");
    assert(0);
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  subkey " + d(subkey));
  }
  free(tmpfile);

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place a human-readable version of the value in the cache, for easier debugging. */
    char* path_debug = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength
                                                      + strlen(kDebugPostfix) + 1));
    memcpy(path_debug, path_dst, base_dir_.length() + kObjCachePathLength);
    memcpy(&path_debug[base_dir_.length() + kObjCachePathLength], kDebugPostfix,
           strlen(kDebugPostfix) + 1);

    FILE *f = fopen(path_debug, "w");
    entry->debug(f);
    fclose(f);
  }
  return true;
}

/**
 * Retrieve an entry from the obj-cache.
 *
 * @param key The key
 * @param subkey The subkey
 * @param[out] entry mmap()-ed cache entry. It is the caller's responsibility to munmap() it later.
 * @param[out] entry_len entry's length in bytes
 * @return Whether succeeded
 */
bool ObjCache::retrieve(const Hash &key,
                        const char* const subkey,
                        uint8_t ** entry,
                        size_t * entry_len) {
  TRACK(FB_DEBUG_CACHING, "key=%s, subkey=%s", D(key), D(subkey));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: retrieving entry, key "
             + d(key) + " subkey " + d(subkey));
  }

  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, subkey, false, path);
  return retrieve(path, entry, entry_len);
}

bool ObjCache::retrieve(const char* path, uint8_t ** entry, size_t * entry_len) {
  TRACK(FB_DEBUG_CACHING, "path=%s", D(path));
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    fb_perror("open");
    assert(0);
    return false;
  }

  struct stat64 st;
  if (fstat64(fd, &st) == -1) {
    fb_perror("Failed fstat retrieving cache object");
    assert(0);
    close(fd);
    return false;
  } else if (!S_ISREG(st.st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
    assert(0);
    close(fd);
    return false;
  }

  uint8_t *p = NULL;
  if (st.st_size > 0) {
    /* Zero bytes can't be mmapped, we're fine with p == NULL then.
     * Although a serialized entry probably can't be 0 bytes long. */
    p = reinterpret_cast<uint8_t*>(mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
      fb_perror("mmap");
      assert(0);
      close(fd);
      return false;
    }
  } else {
    fb_error("0-sized cache entry: " + std::string(path));
    assert(st.st_size <= 0);
    close(fd);
    return false;
  }
  close(fd);

  *entry_len = st.st_size;
  *entry = p;
  return true;
}

void ObjCache::mark_as_used(const Hash &key,
                            const char* const subkey) {
  TRACK(FB_DEBUG_CACHING, "key=%s, subkey=%s", D(key), D(subkey));

  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, subkey, false, path);
  /* Touch the used file. */
  struct timespec times[2] = {{0, UTIME_OMIT}, {0, UTIME_NOW}};
  utimensat(AT_FDCWD, path, times, 0);
}
/**
 * Return the list of subkeys for the given key in the order to be tried for shortcutting.
 *
 * The last created subkey is returned first.
 *
 * // FIXME replace with some iterator-like approach?
 */
static std::vector<AsciiHash> list_subkeys_internal(const char* path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return std::vector<AsciiHash>();
  }

  std::vector<AsciiHash> ret;
  struct dirent *dirent;
  if (!FB_DEBUGGING(FB_DEBUG_CACHE)) {
    while ((dirent = readdir(dir)) != NULL) {
      if (Hash::valid_ascii(dirent->d_name)) {
        ret.push_back(AsciiHash(dirent->d_name));
      }
    }
    struct {
      bool operator()(const AsciiHash a, const AsciiHash b) const { return b < a; }
    } reverse_order;
    std::sort(ret.begin(), ret.end(), reverse_order);
  } else {
    /* Use the subkey's timestamp for sorting since with FB_DEBUG_CACHE the subkey
     * is generated from the file's content, not the creation timestamp. */
    /* Note: Since using a subkey for shortcutting also sets mtime this ordering
     * may not match the ordering without debugging. */
    std::vector<std::pair<AsciiHash, struct timespec>> subkey_timestamp_pairs;
    struct stat st;
    while ((dirent = readdir(dir)) != NULL) {
      if (Hash::valid_ascii(dirent->d_name) && fstatat(dirfd(dir), dirent->d_name, &st, 0) == 0) {
        subkey_timestamp_pairs.push_back({AsciiHash(dirent->d_name), st.st_mtim});
      }
    }
    struct {
      bool operator()(const std::pair<AsciiHash, struct timespec> a,
                      const std::pair<AsciiHash, struct timespec> b) const {
        return timespeccmp(&(b.second), &(a.second), <);
      }
    } reverse_order;
    std::sort(subkey_timestamp_pairs.begin(), subkey_timestamp_pairs.end(), reverse_order);
    for (auto pair : subkey_timestamp_pairs) {
      ret.push_back(pair.first);
    }
  }
  closedir(dir);
  return ret;
}

/**
 * Return the list of subkeys for the given key in the order to be tried for shortcutting.
 *
 * The last created subkey is returned first.
 *
 * // FIXME replace with some iterator-like approach?
 */
std::vector<AsciiHash> ObjCache::list_subkeys(const Hash &key) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_dir_name(base_dir_, key, false, path);
  return list_subkeys_internal(path);
}

void ObjCache::gc_obj_cache_dir(const std::string& path,
                                tsl::hopscotch_set<AsciiHash>* referenced_blobs) {
  DIR * dir = opendir(path.c_str());
  if (dir == NULL) {
    return;
  }

  /* Visit dirs recursively and check all the files. */
  bool valid_ascii_found = false;
  struct dirent *dirent;
  std::vector<std::string> entries_to_delete;
  std::vector<std::string> subdirs_to_visit;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name = dirent->d_name;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }
    switch (fixed_dirent_type(dirent, dir, path)) {
      case DT_DIR: {
        subdirs_to_visit.push_back(name);
        gc_obj_cache_dir(path + "/" + name, referenced_blobs);
        break;
      }
      case DT_REG: {
        if (Hash::valid_ascii(name)) {
          /* Good, will process this later using list_subkeys_internal() to process the subkeys
           * in the order they would be used for shortcutting. */
          valid_ascii_found = true;
        } else {
          /* Regular file, but not named as expected for a cache object. */
          const char* debug_postfix = nullptr;
          if (strcmp(name, kDirDebugJson) == 0) {
            if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
              /* Keeping directory debuuging file, it may be removed with the otherwise empty dir
               * later. */
            } else {
              entries_to_delete.push_back(name);
            }
          } else if ((debug_postfix = strstr(name, kDebugPostfix))) {
            /* Files for debugging cache entries.*/
            if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
              if (debug_postfix) {
                char* related_name = reinterpret_cast<char*>(alloca(debug_postfix - name + 1));
                memcpy(related_name, name, debug_postfix - name);
                related_name[debug_postfix - name] = '\0';
                struct stat st;
                if (fstatat(dirfd(dir), related_name, &st, 0) == 0) {
                  /* Keeping debugging file that has related object. If the object gets removed
                   * the debugging file will be removed with it, too. */
                } else {
                  /* Removing old debugging file later to not break next readdir(). */
                  entries_to_delete.push_back(name);
                }
              } else {
                fb_error("Regular file among cache objects has unexpected name, keeping it: " +
                           path + "/" + name);
              }
            } else {
              /* Removing old debugging file later to not break next readdir(). */
              entries_to_delete.push_back(name);
            }
          } else {
            fb_error("Regular file among cache objects has unexpected name, keeping it: " +
                     path + "/" + name);
          }
        }
        break;
      }
      default:
        fb_error("File's type is unexpected, it is not a directory nor a regular file: " +
                 path + "/" + name);
    }
  }
  for (const auto& entry : entries_to_delete) {
    unlink((path + "/" + entry).c_str());
    if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
      /* All debugging entries were kept in the previous round.
       * Delete the ones related to entries to be deleted. */
      unlink((path + "/" + entry + kDebugPostfix).c_str());
    }
  }
  for (const auto& subdir : subdirs_to_visit) {
    gc_obj_cache_dir(path + "/" + subdir, referenced_blobs);
  }
  /* Process valid entries. */
  if (valid_ascii_found) {
    std::vector<AsciiHash> entries = list_subkeys_internal(path.c_str());
    int usable_entries = 0;
    for (const AsciiHash& entry : entries) {
      uint8_t* entry_buf;
      size_t entry_len;
      if (usable_entries >= shortcut_tries) {
        /* This entry will never be tried. */
        unlinkat(dirfd(dir), entry.c_str(), 0);
        continue;
      }
      if (retrieve((path + "/" + entry.c_str()).c_str(), &entry_buf, &entry_len)) {
        if (execed_process_cacher->is_entry_usable(entry_buf, referenced_blobs)) {
          /* The entry is usable and the referenced blobs were collected.  */
          munmap(entry_buf, entry_len);
          usable_entries++;
        } else {
          /* This entry is not usable, remove it. */
          munmap(entry_buf, entry_len);
          unlinkat(dirfd(dir), entry.c_str(), 0);
        }
      } else {
        fb_error("File's type is unexpected, it is not a directory nor a regular file: " +
                 path + "/" + entry.c_str());
      }
    }
  }

  /* Remove empty directory. */
  rewinddir(dir);
  bool has_valid_entries = false, has_dir_debug_json = false;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name = dirent->d_name;
    /* skip "." and ".." */
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }
    if ((strcmp(name, kDirDebugJson) == 0)) {
      has_dir_debug_json = true;
      continue;
    }
    has_valid_entries = true;
    break;
  }
  if (!has_valid_entries && path != base_dir_) {
    if (has_dir_debug_json) {
      unlinkat(dirfd(dir), kDirDebugJson, 0);
    }
    /* The directory is now empty. It can be removed. */
    rmdir(path.c_str());
  }
  closedir(dir);
}

void ObjCache::gc(tsl::hopscotch_set<AsciiHash>* referenced_blobs) {
  gc_obj_cache_dir(base_dir_, referenced_blobs);
}

}  /* namespace firebuild */
