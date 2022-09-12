/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_OBJ_CACHE_H_
#define FIREBUILD_OBJ_CACHE_H_

#include <string>
#include <vector>

#include "firebuild/ascii_hash.h"
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
  std::vector<AsciiHash> list_subkeys(const Hash &key);

 private:
  /* Including the "objs" subdir. */
  std::string base_dir_;
};

/* singleton */
extern ObjCache *obj_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_OBJ_CACHE_H_
