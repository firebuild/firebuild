/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASHED_FLATBUFFERS_FILE_VECTOR_H_
#define FIREBUILD_HASHED_FLATBUFFERS_FILE_VECTOR_H_

#include <flatbuffers/flatbuffers.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <vector>
#include <algorithm>

#include "firebuild/cache_object_format_generated.h"
#include "firebuild/file_name.h"
#include "firebuild/file_usage.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class HashedFlatbuffersFileVector {
 public:
  explicit HashedFlatbuffersFileVector(flatbuffers::FlatBufferBuilder* builder)
      : builder_(builder) {}
  void add(const FileName* file_name, const Hash& hash,  const int mode = 0) {
    assert(!sorted_);
    const int mtime = 0, size = 0;
    files_.push_back(msg::CreateFile(*builder_,
                                     builder_->CreateString(file_name->c_str(),
                                                            file_name->length()),
                                     builder_->CreateVector(hash.to_binary(),
                                                            Hash::hash_size()),
                                     mtime, size, mode));
    hashes_.push_back({file_name->hash_XXH128(),
            *reinterpret_cast<const XXH128_hash_t*>(hash.to_binary()),
                       {0, static_cast<XXH64_hash_t>(mode)}});
    return;
  }
  void add(const FileName* file_name, const FileUsage* fu) {
    const int mode = 0;
    add(file_name, fu->initial_hash(), mode);
  }
  void add(const FileName* file_name, const int mode = 0) {
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
  XXH128_hash_t hash() const {
    assert(sorted_);
    return XXH3_128bits(hashes_.data(), hashes_.size() * sizeof(XXH128_hash_t));
  }
  std::vector<flatbuffers::Offset<msg::File>>& files() {
    assert(sorted_);
    return files_;
  }

 private:
  struct FileHashTuple {
    XXH128_hash_t name_hash;
    XXH128_hash_t content_hash;
    XXH128_hash_t mode;
  };
  flatbuffers::FlatBufferBuilder* builder_;
  bool sorted_ = false;
  std::vector<flatbuffers::Offset<msg::File>> files_ = {};
  std::vector<FileHashTuple> hashes_ = {};
  DISALLOW_COPY_AND_ASSIGN(HashedFlatbuffersFileVector);
};

}  // namespace firebuild

#endif  // FIREBUILD_HASHED_FLATBUFFERS_FILE_VECTOR_H_
