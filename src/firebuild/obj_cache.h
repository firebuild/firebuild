/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

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
   * @param debug_key Optionally the key as pb for debugging purposes
   * @return Whether succeeded
   */
  bool store(const Hash &key,
             const FBBSTORE_Builder * const entry,
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
  void gc(tsl::hopscotch_set<AsciiHash>* referenced_blobs);

 private:
  void gc_obj_cache_dir(const std::string& path,
                        tsl::hopscotch_set<AsciiHash>* referenced_blobs);

  /* Including the "objs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.json";
  static constexpr char kDirDebugJson[] = "%_directory_debug.json";
};

/* singleton */
extern ObjCache *obj_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_OBJ_CACHE_H_
