/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASHED_FBB_STRING_VECTOR_H_
#define FIREBUILD_HASHED_FBB_STRING_VECTOR_H_

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <vector>
#include <algorithm>

#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class HashedFbbStringVector {
 public:
  HashedFbbStringVector() {}
  void add(const FileName* file_name) {
    assert(!sorted_);
    c_strings_.push_back(file_name->c_str());
    hashes_.push_back(file_name->hash_XXH128());
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
  /* hash of the hashes */
  XXH128_hash_t hash() const {
    assert(sorted_);
    return XXH3_128bits(hashes_.data(), hashes_.size() * sizeof(XXH128_hash_t));
  }
  const std::vector<const char *>& c_strings() const {
    assert(sorted_);
    return c_strings_;
  }

 private:
  std::vector<const char *> c_strings_ = {};
  std::vector<XXH128_hash_t> hashes_ = {};
  bool sorted_ = false;
  DISALLOW_COPY_AND_ASSIGN(HashedFbbStringVector);
};

}  // namespace firebuild

#endif  // FIREBUILD_HASHED_FBB_STRING_VECTOR_H_
