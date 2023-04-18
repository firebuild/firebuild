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

#include "firebuild/blob_cache.h"
#include "firebuild/config.h"
#include "firebuild/debug.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/hash.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"
#include "firebuild/subkey.h"
#include "firebuild/utils.h"

namespace firebuild {

/* singleton */
ObjCache *obj_cache;

ObjCache::ObjCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}


/* /x/xx/<ascii key>/<ascii subkey> */
static size_t kObjCachePathLength =
    1 + 1 + 1 + 2 + 1 + Hash::kAsciiLength + 1 + Subkey::kAsciiLength;

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
  path[base.length() + kObjCachePathLength - Subkey::kAsciiLength - 1] = '/';
  memcpy(&path[base.length() + kObjCachePathLength - Subkey::kAsciiLength], subkey,
         Subkey::kAsciiLength + 1);
}

bool ObjCache::store(const Hash &key,
                     const FBBSTORE_Builder * const entry,
                     off_t stored_blob_bytes,
                     const FBBFP_Serialized * const debug_key) {
  TRACK(FB_DEBUG_CACHING, "key=%s, stored_blob_bytes=%" PRIoff, D(key), stored_blob_bytes);

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: storing entry, key " + d(key));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE) && debug_key) {
    /* Place a human-readable version of the key in the cache, for easier debugging. */
    char* path_debug =
        reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength
                                       - Subkey::kAsciiLength + 1 +strlen(kDirDebugJson) + 1));
    construct_cached_dir_name(base_dir_, key, true, path_debug);
    path_debug[base_dir_.length() + kObjCachePathLength - Subkey::kAsciiLength - 1] = '/';
    memcpy(&path_debug[base_dir_.length() + kObjCachePathLength - Subkey::kAsciiLength],
           kDirDebugJson, strlen(kDirDebugJson) + 1);

    FILE *f = fopen(path_debug, "wx");
    if (f) {
      debug_key->debug(f);
      execed_process_cacher->update_cached_bytes(ftell(f));
      fclose(f);
    }
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
  if (stored_blob_bytes + len > max_entry_size) {
    FB_DEBUG(FB_DEBUG_CACHING,
             "Could not store entry in cache because it would exceed max_entry_size");
    free(tmpfile);
    close(fd_dst);
    return false;
  }

  char *entry_serial = reinterpret_cast<char *>(malloc(len));
  entry->serialize(entry_serial);
  fb_write(fd_dst, entry_serial, len);
  close(fd_dst);

  /* Create randomized object file */
  char* path_dst = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  /* Store both seconds and nanoseconds in 64 bits.
   * The seconds since the epoch is stored in the first 34 bits
   * that will be enough until 2514 and the nanoseconds are stored in the next 30. */
  Subkey subkey =
      Subkey((static_cast<uint64_t>(time.tv_sec) << 30) + static_cast<uint64_t>(time.tv_nsec));
  if (FB_DEBUGGING(FB_DEBUG_DETERMINISTIC_CACHE)) {
    /* Debugging: Instead of a randomized filename (which is fast to generate) use the content's
     * hash for a deterministic filename. */
    XXH128_hash_t entry_hash = XXH3_128bits(entry_serial, len);
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, entry_hash);
    /* Use only the first part of the digest in for the subkey. */
    // TODO(rbalint) switching to XXH64_hash_t somehow does not match xxh64sum's output, debug that
    subkey = Subkey(canonical.digest);
  }

  construct_cached_file_name(base_dir_, key, subkey.c_str(), true, path_dst);
  free(entry_serial);

  if (fb_renameat2(AT_FDCWD, tmpfile, AT_FDCWD, path_dst, RENAME_NOREPLACE) == -1) {
    if (errno == EEXIST) {
      FB_DEBUG(FB_DEBUG_CACHING, "cache object is already stored");
      unlink(tmpfile);
      return true;
    } else {
      fb_perror("Failed rename() while storing cache object");
      assert(0);
      unlink(tmpfile);
      free(tmpfile);
      return false;
    }
  } else {
    execed_process_cacher->update_cached_bytes(len);
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

    FILE *f = fopen(path_debug, "wx");
    if (f) {
      entry->debug(f);
      execed_process_cacher->update_cached_bytes(ftell(f));
      fclose(f);
    }
  }
  return true;
}

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
static std::vector<Subkey> list_subkeys_internal(const char* path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return std::vector<Subkey>();
  }

  std::vector<Subkey> ret;
  struct dirent *dirent;
  if (!FB_DEBUGGING(FB_DEBUG_CACHE)) {
    while ((dirent = readdir(dir)) != NULL) {
      if (Subkey::valid_ascii(dirent->d_name)) {
        ret.push_back(Subkey(dirent->d_name));
      }
    }
    struct {
      bool operator()(const Subkey a, const Subkey b) const { return b < a; }
    } reverse_order;
    std::sort(ret.begin(), ret.end(), reverse_order);
  } else {
    /* Use the subkey's timestamp for sorting since with FB_DEBUG_CACHE the subkey
     * is generated from the file's content, not the creation timestamp. */
    /* Note: Since using a subkey for shortcutting also sets mtime this ordering
     * may not match the ordering without debugging. */
    std::vector<std::pair<Subkey, struct timespec>> subkey_timestamp_pairs;
    struct stat st;
    while ((dirent = readdir(dir)) != NULL) {
      if (Subkey::valid_ascii(dirent->d_name) && fstatat(dirfd(dir), dirent->d_name, &st, 0) == 0) {
        subkey_timestamp_pairs.push_back({Subkey(dirent->d_name), st.st_mtim});
      }
    }
    struct {
      bool operator()(const std::pair<Subkey, struct timespec> a,
                      const std::pair<Subkey, struct timespec> b) const {
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
std::vector<Subkey> ObjCache::list_subkeys(const Hash &key) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_dir_name(base_dir_, key, false, path);
  return list_subkeys_internal(path);
}

static void gc_collect_obj_timestamp_sizes_internal(
    const std::string& path,
    std::vector<obj_timestamp_size_t>* obj_timestamp_sizes) {
  DIR * dir = opendir(path.c_str());
  if (dir == NULL) {
    return;
  }

  /* Visit dirs recursively and collect all the files named as valid subkeys. */
  struct dirent *dirent;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name = dirent->d_name;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }
    switch (fixed_dirent_type(dirent, dir, path)) {
      case DT_DIR: {
        gc_collect_obj_timestamp_sizes_internal(path + "/" + name, obj_timestamp_sizes);
        break;
      }
      case DT_REG: {
        struct stat st;
        if (Subkey::valid_ascii(name) && fstatat(dirfd(dir), name, &st, 0) == 0) {
          obj_timestamp_sizes->push_back({path + "/" + name, st.st_mtim, st.st_size});
        }
        break;
      }
      default:
        /* Just ignore the file which is not a cache object named as a valid subkey. */
        break;
    }
  }
  closedir(dir);
}

std::vector<obj_timestamp_size_t>
ObjCache::gc_collect_sorted_obj_timestamp_sizes() {
  std::vector<obj_timestamp_size_t> obj_timestamp_sizes;
  gc_collect_obj_timestamp_sizes_internal(base_dir_, &obj_timestamp_sizes);
  struct {
    bool operator()(const obj_timestamp_size_t& a,
                    const obj_timestamp_size_t& b) const {
      return timespeccmp(&(b.ts), &(a.ts), <);
    }
  } reverse;
  std::sort(obj_timestamp_sizes.begin(), obj_timestamp_sizes.end(), reverse);
  return obj_timestamp_sizes;
}

off_t ObjCache::gc_collect_total_objects_size() {
  return recursive_total_file_size(base_dir_);
}

void ObjCache::gc_obj_cache_dir(const std::string& path,
                                tsl::hopscotch_set<AsciiHash>* referenced_blobs,
                                off_t* cache_bytes, off_t* debug_bytes,
                                off_t* unexpected_file_bytes) {
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
        break;
      }
      case DT_REG: {
        if (Subkey::valid_ascii(name)) {
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
              *debug_bytes += file_size(dir, name);
            } else {
              entries_to_delete.push_back(name);
            }
          } else if ((debug_postfix = strstr(name, kDebugPostfix))) {
            /* Files for debugging cache entries.*/
            if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
              char* related_name = reinterpret_cast<char*>(alloca(debug_postfix - name + 1));
              memcpy(related_name, name, debug_postfix - name);
              related_name[debug_postfix - name] = '\0';
              struct stat st;
              if (fstatat(dirfd(dir), related_name, &st, 0) == 0) {
                /* Keeping debugging file that has related object. If the object gets removed
                 * the debugging file will be removed with it, too. */
                *debug_bytes += file_size(dir, name);
              } else {
                /* Removing old debugging file later to not break next readdir(). */
                entries_to_delete.push_back(name);
              }
            } else {
              /* Removing old debugging file later to not break next readdir(). */
              entries_to_delete.push_back(name);
            }
          } else {
            fb_error("Regular file among cache objects has unexpected name, keeping it: " +
                     path + "/" + name);
            *unexpected_file_bytes += file_size(dir, name);
          }
        }
        break;
      }
      default:
        fb_error("File's type is unexpected, it is not a directory nor a regular file: " +
                 path + "/" + name);
    }
  }
  /* This actually deletes entries from here, the ObjCache,
   * just uses the implementation in BlobCache. */
  BlobCache::delete_entries(path, entries_to_delete, kDebugPostfix, debug_bytes);
  for (const auto& subdir : subdirs_to_visit) {
    gc_obj_cache_dir(path + "/" + subdir, referenced_blobs, cache_bytes, debug_bytes,
                     unexpected_file_bytes);
  }
  /* Process valid entries. */
  if (valid_ascii_found) {
    std::vector<Subkey> entries = list_subkeys_internal(path.c_str());
    int usable_entries = 0;
    for (const Subkey& entry : entries) {
      uint8_t* entry_buf;
      size_t entry_len;
      if (usable_entries >= shortcut_tries) {
        /* This entry will never be tried. */
        struct stat st;
        if (fstatat(dirfd(dir), entry.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
          if (unlinkat(dirfd(dir), entry.c_str(), 0) == 0) {
            execed_process_cacher->update_cached_bytes(-st.st_size);
          } else {
            fb_perror("unlinkat");
          }
        } else {
          fb_perror("fstatat");
        }
        continue;
      }
      if (retrieve((path + "/" + entry.c_str()).c_str(), &entry_buf, &entry_len)) {
        if (execed_process_cacher->is_entry_usable(entry_buf, referenced_blobs)) {
          /* The entry is usable and the referenced blobs were collected.  */
          munmap(entry_buf, entry_len);
          usable_entries++;
          *cache_bytes += entry_len;
        } else {
          /* This entry is not usable, remove it. */
          munmap(entry_buf, entry_len);
          if (unlinkat(dirfd(dir), entry.c_str(), 0) == 0) {
            execed_process_cacher->update_cached_bytes(-entry_len);
          } else {
            fb_perror("unlinkat");
          }
        }
      } else {
        fb_error("File's type is unexpected, it is not a directory nor a regular file: " +
                 path + "/" + entry.c_str());
        *unexpected_file_bytes += file_size(nullptr, (path + "/" + entry.c_str()).c_str());
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
      struct stat st;
      if (fstatat(dirfd(dir), kDirDebugJson, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (unlinkat(dirfd(dir), kDirDebugJson, 0) == 0) {
          execed_process_cacher->update_cached_bytes(-st.st_size);
          *debug_bytes -= st.st_size;
        } else {
          fb_perror("unlinkat");
        }
      } else {
        fb_perror(kDebugPostfix);
      }
    }
    /* The directory is now empty. It can be removed. */
    rmdir(path.c_str());
  }
  closedir(dir);
}

void ObjCache::gc(tsl::hopscotch_set<AsciiHash>* referenced_blobs, off_t* cache_bytes,
                  off_t* debug_bytes, off_t* unexpected_file_bytes) {
  gc_obj_cache_dir(base_dir_, referenced_blobs, cache_bytes, debug_bytes, unexpected_file_bytes);
}

}  /* namespace firebuild */
