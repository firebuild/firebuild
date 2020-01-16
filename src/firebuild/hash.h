/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_H_
#define FIREBUILD_HASH_H_

#include <xxhash.h>

#include <string>

namespace firebuild {

class Hash {
 public:
  Hash()
      :arr()
  {}
  unsigned char arr[8] = {};
  bool set(const std::string &from_file);
};

std::string to_string(Hash const&);

}  // namespace firebuild
#endif  // FIREBUILD_HASH_H_
