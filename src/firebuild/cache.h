/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_CACHE_H_
#define FIREBUILD_CACHE_H_

#include <google/protobuf/message_lite.h>

#include <string>

#include "firebuild/hash.h"

namespace firebuild {

class Cache {
 public:
  explicit Cache(const std::string &base_dir);
  ~Cache();

  bool store_file(const std::string &path,
                  Hash *key_out);
  bool retrieve_file(const Hash &key,
                     const std::string &path_dst);

 private:
  /* Including the "blobs" subdir. */
  std::string base_dir_;
};

}  // namespace firebuild
#endif  // FIREBUILD_CACHE_H_
