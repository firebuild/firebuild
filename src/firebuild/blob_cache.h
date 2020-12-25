/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_BLOB_CACHE_H_
#define FIREBUILD_BLOB_CACHE_H_

#include <string>

#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

class BlobCache {
 public:
  explicit BlobCache(const std::string &base_dir);
  ~BlobCache();

  bool store_file(const FileName *path,
                  Hash *key_out);
  bool retrieve_file(const Hash &key,
                     const FileName *path_dst);

 private:
  /* Including the "blobs" subdir. */
  std::string base_dir_;
};

/* singleton */
extern BlobCache *blob_cache;

}  // namespace firebuild
#endif  // FIREBUILD_BLOB_CACHE_H_
