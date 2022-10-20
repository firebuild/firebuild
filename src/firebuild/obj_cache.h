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

class ObjCache {
 public:
  explicit ObjCache(const std::string &base_dir);
  ~ObjCache();

  bool store(const Hash &key,
             const FBBSTORE_Builder * const entry,
             const FBBFP_Serialized * const debug_key);
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
  void gc(tsl::hopscotch_set<AsciiHash>* referenced_blobs, ssize_t* cache_bytes,
          ssize_t* debug_bytes, ssize_t* unexpected_file_bytes);

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
                        tsl::hopscotch_set<AsciiHash>* referenced_blobs, ssize_t* cache_bytes,
                        ssize_t* debug_bytes, ssize_t* unexpected_file_bytes);

  /* Including the "objs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.json";
  static constexpr char kDirDebugJson[] = "%_directory_debug.json";
};

/* singleton */
extern ObjCache *obj_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_OBJ_CACHE_H_
