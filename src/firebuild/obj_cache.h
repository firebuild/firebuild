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

#ifndef FIREBUILD_OBJ_CACHE_H_
#define FIREBUILD_OBJ_CACHE_H_

#include <tsl/hopscotch_set.h>

#include <string>
#include <vector>

#include "firebuild/subkey.h"
#include "firebuild/hash.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"

namespace firebuild {

typedef struct obj_timestamp_size_ {
  std::string obj {""};
  struct timespec ts {0, 0};
  off_t size {0};
} obj_timestamp_size_t;

/**
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
class ObjCache {
 public:
  explicit ObjCache(const std::string &base_dir);
  ~ObjCache();

  /**
   * Store a serialized entry in obj-cache.
   *
   * @param key The key
   * @param entry The entry to serialize and store
   * @param stored_blob_bytes Total size of blobs referenced by this obj
   * @param debug_key Optionally the key as pb for debugging purposes
   * @return Whether succeeded
   */
  bool store(const Hash &key,
             const FBBSTORE_Builder * const entry,
             off_t stored_blob_bytes,
             const FBBFP_Serialized * const debug_key);
  /**
   * Retrieve an entry from the obj-cache.
   *
   * @param key The key
   * @param subkey The subkey
   * @param[out] entry mmap()-ed cache entry. It is the caller's responsibility to munmap() it later.
   * @param[out] entry_len entry's length in bytes
   * @return Whether succeeded
   */
  bool retrieve(const Hash &key,
                const char * const subkey,
                uint8_t ** entry,
                size_t * entry_len);
  bool retrieve(const char* path,
                uint8_t ** entry,
                size_t * entry_len);
  void mark_as_used(const Hash &key, const char * const subkey);
  std::vector<Subkey> list_subkeys(const Hash &key);
  /**
   * Garbage collect the object cache
   * @param referenced_blobs blobs referenced from the object cache entries. It is updated while
   *        processing the cache objects.
   * @param[in,out] cache_bytes increased by every found and kept obj's size
   * @param[in,out] debug_bytes increased by every found and kept debug file's size
   * @param[in,out] unexpected_file_bytes increased by every found and kept file's size that has
                    unexpected name, i.e. it is not used as a cache object, nor a debug file
   */
  void gc(tsl::hopscotch_set<AsciiHash>* referenced_blobs, off_t* cache_bytes,
          off_t* debug_bytes, off_t* unexpected_file_bytes);
  /* Returns {object path, timestamp, size} ordered by decreasing timestamp. */
  std::vector<obj_timestamp_size_t> gc_collect_sorted_obj_timestamp_sizes();
  /** Returns total size of all stored objects including debug and invalid entries. */
  off_t gc_collect_total_objects_size();

 private:
  /**
   * Garbage collect an object cache directory
   * @param path object cache directory's absolute path
   * @param referenced_blobs blobs referenced from the object cache entries. It is updated while
   *        processing the cache objects.
   * @param[in,out] cache_bytes increased by every found and kept obj's size
   * @param[in,out] debug_bytes increased by every found and kept debug file's size
   * @param[in,out] unexpected_file_bytes increased by every found and kept file's size that has
            unexpected name, i.e. it is not used as a cache object, nor a debug file
   */
  void gc_obj_cache_dir(const std::string& path,
                        tsl::hopscotch_set<AsciiHash>* referenced_blobs, off_t* cache_bytes,
                        off_t* debug_bytes, off_t* unexpected_file_bytes);

  /* Including the "objs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.json";
  static constexpr char kDirDebugJson[] = "%_directory_debug.json";
};
/* singleton */
extern ObjCache *obj_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_OBJ_CACHE_H_
