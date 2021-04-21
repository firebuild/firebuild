/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASHED_FLATBUFFERS_STRING_VECTOR_H_
#define FIREBUILD_HASHED_FLATBUFFERS_STRING_VECTOR_H_

#include <flatbuffers/flatbuffers.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <vector>
#include <algorithm>

#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class HashedFlatbuffersStringVector {
 public:
  explicit HashedFlatbuffersStringVector(flatbuffers::FlatBufferBuilder* builder)
      : builder_(builder) {}
  void add(const FileName* file_name) {
    assert(!sorted_);
    strings_.push_back(builder_->CreateString(file_name->c_str(), file_name->length()));
    hashes_.push_back(file_name->hash_XXH128());
    return;
  }
  void sort_hashes() {
    struct {
      bool operator()(const  XXH128_hash_t& hash1, const  XXH128_hash_t& hash2) const {
        return hash1.high64 < hash2.high64
                              || (hash1.high64 == hash2.high64 && hash1.low64 < hash2.low64);
      }
    } hashes_less;
    std::sort(hashes_.begin(), hashes_.end(), hashes_less);
    sorted_ = true;
  }
  XXH128_hash_t hash() const {
    assert(sorted_);
    return XXH3_128bits(hashes_.data(), hashes_.size() * sizeof(XXH128_hash_t));
  }
  std::vector<flatbuffers::Offset<flatbuffers::String>>& strings() {
    assert(sorted_);
    return strings_;
  }

 private:
  flatbuffers::FlatBufferBuilder* builder_;
  std::vector<XXH128_hash_t> hashes_ = {};
  bool sorted_ = false;
  std::vector<flatbuffers::Offset<flatbuffers::String>> strings_ = {};
  DISALLOW_COPY_AND_ASSIGN(HashedFlatbuffersStringVector);
};

}  // namespace firebuild

#endif  // FIREBUILD_HASHED_FLATBUFFERS_STRING_VECTOR_H_
