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

/**
 * This class implements a global (that is, once per firebuild process) in-memory cache of file
 * hashes.
 *
 * This cache stores the hash of files and directories that are found, along with some statinfo that
 * lets determine if the hash needs to be refreshed.
 *
 * Internally, different strategies are used for files under system (read-only) locations (as per
 * the config file) and for non-system (read-write) locations. The public API completely hides this
 * and provides a unified interface for both types.
 *
 * For system locations we expect that they don't change during Firebuild's lifetime. Once cached,
 * the actual file is no longer checked. Nonexisting files are also cached.
 *
 * (Note though: Currently as soon as someone asks for the file's size or permissions, we compute
 * its checksum too. It's very unlikely that a build procedure stat()s a system file successfully
 * and then does not read it. In turn, the code becomes simpler, it doesn't have to cope with files
 * whose size is already cached but the checksum isn't yet. This might change in the future.)
 *
 * For non-system locations we always begin by stat()ing the file, and the cached checksum is
 * forgotten in case of statinfo mismatch. Accordingly, negative entries aren't cached, it just
 * wouldn't make sense.
 */
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
   * @param max_writers  maximum allowed number of writers to this file
   * @param[out] hash    hash to retrive/calculate
   * @param[out] is_dir  optionally store if path is a dir
   * @param[out] size    optionally store the size if it's a regular file
   * @param fd           if >= 0 then read the file from there
   * @param stat_ptr     optionally the file's parameters already stat()'ed
   * @return             false if not a regular file or directory
   */
  bool get_hash(const FileName* path, int max_writers, Hash *hash, bool *is_dir = nullptr,
                ssize_t *size = nullptr, int fd = -1,
                const struct stat64 *stat_ptr = nullptr);

  /**
   * Return the hash of a regular file. Also store this file in the blob cache.
   *
   * @param path              file's path
   * @param max_writers       maximum allowed number of writers to this file
   * @param[out] hash         hash to retrive/calculate
   * @param[out] stored_bytes bytes stored to the blob cache
   * @param fd                if >= 0 then read the file from there
   * @param stat_ptr          optionally the file's parameters already stat()'ed
   * @return                  false if not a regular file or directory
   */
  bool store_and_get_hash(const FileName* path, int max_writers, Hash *hash, off_t* stored_bytes,
                          int fd, const struct stat64 *stat_ptr);

  /**
   * Check if the given FileInfo query matches the file system.
   *
   * @param path   file's path
   * @param query  the query to match against
   * @return       whether the query matches the file
   */
  bool file_info_matches(const FileName *path, const FileInfo& query);

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
   * @param max_writers           maximum allowed number of writers to this file
   * @param fd                    if >= 0 then read the file from there
   * @param stat_ptr              optionally the file's parameters already stat()'ed
   * @param store                 whether to store the file in the blob cache
   * @param[out] stored_bytes     bytes stored to the blob cache
   * @param skip_statinfo_update  assume that the stat info is up-to-date
   * @return                      the requested information about the file
   */
  const HashCacheEntry* get_entry_with_statinfo_and_hash(const FileName* path, int max_writers,
                                                         int fd, const struct stat64 *stat_ptr,
                                                         bool store, off_t* stored_bytes,
                                                         bool skip_statinfo_update = false);

  /**
   * A singleton structure representing a file system path that does not point to a regular file or
   * directory. get_entry_...() might return its address.
   */
  static const HashCacheEntry notexist_;
  /**
   * A singleton structure representing a file system path with an unknown state for the supervisor.
   * get_entry_...() might return its address.
   */
  static const HashCacheEntry dontknow_;

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
