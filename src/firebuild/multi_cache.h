/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_MULTI_CACHE_H_
#define FIREBUILD_MULTI_CACHE_H_

#include <google/protobuf/message_lite.h>

#include <string>

#include "firebuild/hash.h"

namespace firebuild {

class MultiCache {
 public:
  MultiCache(const std::string &base_dir);
  ~MultiCache();

  bool store_protobuf(const Hash &key,
                      const google::protobuf::Message &msg,
                      const std::string &debug_header,
                      Hash *subkey_out);
  bool retrieve_protobuf(const Hash &key,
                         const Hash &subkey,
                         google::protobuf::MessageLite *msg);
  std::vector<Hash> list_subkeys(const Hash &key);

 private:
  std::string base_dir_;
};

}  // namespace firebuild
#endif  // FIREBUILD_MULTI_CACHE_H_
