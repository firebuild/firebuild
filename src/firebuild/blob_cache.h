/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_BLOB_CACHE_H_
#define FIREBUILD_BLOB_CACHE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <tsl/hopscotch_set.h>
#include <unistd.h>

#include <string>

#include "firebuild/ascii_hash.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

class BlobCache {
 public:
  explicit BlobCache(const std::string &base_dir);
  ~BlobCache();

  bool store_file(const FileName *path,
                  int max_writers,
                  int fd_src,
                  loff_t src_skip_bytes,
                  size_t size,
                  Hash *key_out);
  bool move_store_file(const std::string &path,
                       int fd,
                       size_t size,
                       Hash *key_out);
  bool retrieve_file(const Hash &key,
                     const FileName *path_dst,
                     bool append);
  int get_fd_for_file(const Hash &key);
  void gc(const tsl::hopscotch_set<AsciiHash>& referenced_blobs);

 private:
  void gc_blob_cache_dir(const std::string& path,
                         const tsl::hopscotch_set<AsciiHash>& referenced_blobs);
  /* Including the "blobs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.txt";
};

/* singleton */
extern BlobCache *blob_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_BLOB_CACHE_H_
