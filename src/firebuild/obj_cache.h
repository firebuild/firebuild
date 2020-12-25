/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_OBJ_CACHE_H_
#define FIREBUILD_OBJ_CACHE_H_

#include <string>
#include <vector>

#include "firebuild/hash.h"

namespace firebuild {

class ObjCache {
 public:
  explicit ObjCache(const std::string &base_dir);
  ~ObjCache();

  bool store(const Hash &key,
             const uint8_t * const entry,
             const size_t entry_len,
             const uint8_t * const debug_key,
             Hash *subkey_out);
  bool retrieve(const Hash &key,
                const Hash &subkey,
                uint8_t ** entry,
                size_t * entry_len);
  std::vector<Hash> list_subkeys(const Hash &key);

 private:
  /* Including the "objs" subdir. */
  std::string base_dir_;
};

/* singleton */
extern ObjCache *obj_cache;

}  // namespace firebuild
#endif  // FIREBUILD_OBJ_CACHE_H_
