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

  /**
   * Store the given regular file in the blob cache, with its hash as the key.
   * Uses advanced technologies, such as copy on write, if available.
   *
   * If fd >= 0 then that is used as the data source, the path is only used for debugging.
   *
   * @param path The file to place in the cache
   * @param max_writers Maximum allowed number of writers to this file
   * @param fd_src Optionally the opened file descriptor to copy
   * @param src_skip_bytes Number of bytes to omit from the beginning of the input file
   * @param size The file's size (including the bytes to be skipped)
   * @param key_out Optionally store the key (hash) here
   * @return Whether succeeded
   */
  bool store_file(const FileName *path,
                  int max_writers,
                  int fd_src,
                  loff_t src_skip_bytes,
                  loff_t size,
                  Hash *key_out);
  /**
   * Store the given regular file in the blob cache, with its hash as the key.
   *
   * The file is moved from its previous location. It is assumed that
   * no one modifies it during checksum computation, that is, the
   * intercepted processes have no direct access to it.
   *
   * The file handle is closed.
   *
   * The hash_cache is not queried or updated.
   *
   * This API is designed for PipeRecorder in order to place the recorded data in the cache.
   *
   * @param path The file to move to the cache
   * @param fd A fd referring to this file
   * @param size The file's size
   * @param key_out Optionally store the key (hash) here
   * @return Whether succeeded
   */
  bool move_store_file(const std::string &path,
                       int fd,
                       loff_t size,
                       Hash *key_out);
  /**
   * Retrieve the given file from the blob cache.
   *
   * In non-append mode the file doesn't have to exist. If it doesn't exist, it's created with the
   * default permissions, according to the current umask. If it already exists, its contents will be
   * replaced, the permissions will be left unchanged.
   *
   * In append mode the file must already exist, the cache entry will be appended to it.
   *
   * Uses advanced technologies, such as copy on write, if available.
   *
   * @param blob_fd opened file descriptor of the blob to be used
   * @param path_dst Where to place the file
   * @param append Whether to use append mode
   * @return Whether succeeded
   */
  bool retrieve_file(int blob_fd,
                     const FileName *path_dst,
                     bool append);
  /**
   * Get a read-only fd for a given entry in the cache.
   *
   * This is comfy when shortcutting a process and replaying what it wrote to a pipe.
   *
   * @param key The key (the file's hash)
   * @return A read-only fd, or -1
   */
  int get_fd_for_file(const Hash &key);
  /**
   * Garbage collect the blob cache
   * @param referenced_blobs blobs referenced from the object cache, they won't be deleted
   * @param[in,out] cache_bytes increased by every found and kept blob's size
   * @param[in,out] debug_bytes increased by every found and kept debug file's size
   * @param[in,out] unexpected_file_bytes increased by every found and kept file's size that has
                    unexpected name, i.e. it is not used as a blob, nor a debug file
   */
  void gc(const tsl::hopscotch_set<AsciiHash>& referenced_blobs, off_t* cache_bytes,
          off_t* debug_bytes, off_t* unexpected_file_bytes);
  /**
   * Delete entries on the the specified path also deleting the debug entries related to the entries
   * to delete.
   * @param path path where the entries reside
   * @param entries entries to delete
   * @param debug_postfix string to prepend to entries to get the related debug entries
   * @param[in,out] debug_bytes decremented when removing a debug entry
   */
  static void delete_entries(const std::string& path, const std::vector<std::string>& entries,
                             const std::string& debug_postfix, off_t* debug_bytes);

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
                         off_t* cache_bytes, off_t* debug_bytes,
                         off_t* unexpected_file_bytes);
  /* Including the "blobs" subdir. */
  std::string base_dir_;
  static constexpr char kDebugPostfix[] = "_debug.txt";
};

/* singleton */
extern BlobCache *blob_cache;

}  /* namespace firebuild */
#endif  // FIREBUILD_BLOB_CACHE_H_
