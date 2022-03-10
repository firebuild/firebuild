/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_BLOB_CACHE_H_
#define FIREBUILD_BLOB_CACHE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

class BlobCache {
 public:
  explicit BlobCache(const std::string &base_dir);
  ~BlobCache();

  bool store_file(const FileName *path,
                  int fd_src,
                  const struct stat64 *stat_ptr,
                  Hash *key_out);
  bool move_store_file(const std::string &path,
                       int fd,
                       size_t len,
                       Hash *key_out);
  bool retrieve_file(const Hash &key,
                     const FileName *path_dst);
  int get_fd_for_file(const Hash &key);

 private:
  /* Including the "blobs" subdir. */
  std::string base_dir_;
};

/* singleton */
extern BlobCache *blob_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_BLOB_CACHE_H_
