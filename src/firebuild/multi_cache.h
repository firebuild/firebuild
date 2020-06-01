/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_MULTI_CACHE_H_
#define FIREBUILD_MULTI_CACHE_H_


#include <string>
#include <vector>

#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>

#include "firebuild/hash.h"

namespace firebuild {

class MultiCache {
 public:
  explicit MultiCache(const std::string &base_dir);
  ~MultiCache();

  bool store_protobuf(const Hash &key,
                      const google::protobuf::Message &msg,
                      const google::protobuf::Message *debug_key,
                      const std::string &debug_header,
                      const google::protobuf::TextFormat::Printer *printer,
                      Hash *subkey_out);
  bool retrieve_protobuf(const Hash &key,
                         const Hash &subkey,
                         google::protobuf::MessageLite *msg);
  std::vector<Hash> list_subkeys(const Hash &key);

 private:
  /* Including the "pbs" subdir. */
  std::string base_dir_;
};

}  // namespace firebuild
#endif  // FIREBUILD_MULTI_CACHE_H_
