/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_SHA256HASH_H
#define FIREBUILD_SHA256HASH_H

#include <openssl/sha.h>

#include <string>

namespace firebuild {

class SHA256Hash {
 public:
  SHA256Hash()
      :arr()
  {}
  unsigned char arr[SHA256_DIGEST_LENGTH] = {};
  int update(const std::string &from_file);
};

}  // namespace firebuild
#endif
