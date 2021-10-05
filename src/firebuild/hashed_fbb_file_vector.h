/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASHED_FBB_FILE_VECTOR_H_
#define FIREBUILD_HASHED_FBB_FILE_VECTOR_H_

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <vector>
#include <algorithm>

#include "firebuild/file_name.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class HashedFbbFileVector {
 public:
  HashedFbbFileVector() {}
  void add(const FileName* file_name, const Hash& content_hash, const int mode = -1) {
    assert(!sorted_);
    FBBSTORE_Builder_file& new_file = files_.emplace_back();
    fbbstore_builder_file_init(&new_file);
    fbbstore_builder_file_set_path_with_length(&new_file, file_name->c_str(), file_name->length());
    fbbstore_builder_file_set_hash(&new_file, content_hash.to_canonical());
    if (mode != -1) fbbstore_builder_file_set_mode(&new_file, mode);
    hashes_.push_back({file_name->hash_XXH128(),
                       *reinterpret_cast<const XXH128_hash_t*>(content_hash.to_binary()),
                       {0, static_cast<XXH64_hash_t>(mode)}});
  }
  void add(const FileName* file_name, const FileUsage* fu) {
    add(file_name, fu->initial_hash());
  }
  void add(const FileName* file_name, const int mode = -1) {
    add(file_name, Hash(), mode);
  }
  void sort_hashes() {
    assert(!sorted_);
    /* hashes_ contains {file name hash, file hash, mode} tuples and it should be sorted
     * by file name hash. */
    struct {
      bool operator()(const  FileHashTuple& t1, const  FileHashTuple& t2) const {
        return t1.name_hash.high64 < t2.name_hash.high64
                                     || (t1.name_hash.high64 == t2.name_hash.high64
                                         && t1.name_hash.low64 < t2.name_hash.low64);
      }
    } hashes_less;
    std::sort(hashes_.begin(), hashes_.end(), hashes_less);
    sorted_ = true;
  }
  /* hash of the name hashes, content hashes and modes */
  XXH128_hash_t hash() const {
    assert(sorted_);
    return XXH3_128bits(hashes_.data(), hashes_.size() * sizeof(FileHashTuple));
  }
  size_t size() const {
    return files_.size();
  }
  static const FBBSTORE_Builder *item_fn(int idx, const void *user_data) {
    const HashedFbbFileVector *hashed_fbb_file_vector =
        reinterpret_cast<const HashedFbbFileVector *>(user_data);
    const FBBSTORE_Builder_file *ret = &hashed_fbb_file_vector->files_[idx];
    return reinterpret_cast<const FBBSTORE_Builder *>(ret);
  }

 private:
  struct FileHashTuple {
    XXH128_hash_t name_hash;
    XXH128_hash_t content_hash;
    XXH128_hash_t mode;
  };
  std::vector<FBBSTORE_Builder_file> files_ = {};
  std::vector<FileHashTuple> hashes_ = {};
  bool sorted_ = false;
  DISALLOW_COPY_AND_ASSIGN(HashedFbbFileVector);
};

}  // namespace firebuild

#endif  // FIREBUILD_HASHED_FBB_FILE_VECTOR_H_
