/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_BLOB_CACHE_H_
#define FIREBUILD_BLOB_CACHE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <tsl/hopscotch_set.h>
#include <unistd.h>

#include <string>
#include <vector>

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
  bool retrieve_file(int blob_fd,
                     const FileName *path_dst,
                     bool append);
  int get_fd_for_file(const Hash &key);
  /**
   * Garbage collect the blob cache
   * @param referenced_blobs blobs referenced from the object cache, they won't be deleted
   * @param[in,out] cache_bytes increased by every found and kept blob's size
   * @param[in,out] debug_bytes increased by every found and kept debug file's size
   * @param[in,out] unexpected_file_bytes increased by every found and kept file's size that has
                    unexpected name, i.e. it is not used as a blob, nor a debug file
   */
  void gc(const tsl::hopscotch_set<AsciiHash>& referenced_blobs, ssize_t* cache_bytes,
          ssize_t* debug_bytes, ssize_t* unexpected_file_bytes);
  /**
   * Delete entries on the the specified path also deleting the debug entries related to the entries
   * to delete.
   * @param path path where the entries reside
   * @param entries entries to delete
   * @param debug_postfix string to prepend to entries to get the related debug entries
   * @param[in,out] debug_bytes decremented when removing a debug entry
   */
  static void delete_entries(const std::string& path, const std::vector<std::string>& entries,
                             const std::string& debug_postfix, ssize_t* debug_bytes);

 private:
  /**
   * Garbage collect a blob cache directory
   * @param path blob cache directory's absolute path
   * @param referenced_blobs blobs referenced from the object cache, they won't be deleted
   * @param[in,out] cache_bytes increased by every found and kept blob's size
   * @param[in,out] debug_bytes increased by every found and kept debug file's size
   * @param[in,out] unexpected_file_bytes increased by every found and kept file's size that has
                    unexpected name, i.e. it is not used as a blob, nor a debug file
   */
  void gc_blob_cache_dir(const std::string& path,
                         const tsl::hopscotch_set<AsciiHash>& referenced_blobs,
                         ssize_t* cache_bytes, ssize_t* debug_bytes,
                         ssize_t* unexpected_file_bytes);
  /* Including the "blobs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.txt";
};

/* singleton */
extern BlobCache *blob_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_BLOB_CACHE_H_
