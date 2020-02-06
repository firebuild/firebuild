/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_HASH_H_
#define FIREBUILD_HASH_H_

#include <xxhash.h>

#include <google/protobuf/message_lite.h>

#include <string>

namespace firebuild {

class Hash {
 public:
  Hash()
      :arr_()
  {}

  Hash& operator=(const Hash& src) {
    memcpy(&arr_, &src.arr_, hash_size_);
    return *this;
  }

  static unsigned int hash_size() {return hash_size_;}

  void set_from_data(const void *data, ssize_t size);
  void set_from_protobuf(const google::protobuf::MessageLite &msg);
  bool set_from_fd(int fd, bool *is_dir_out);
  bool set_from_file(const std::string &filename, bool *is_dir_out = NULL);

  bool set_hash_from_hex(const std::string &hex);
  std::string to_binary() const;
  std::string to_hex() const;

 private:
  static const unsigned int hash_size_ = 8;
  char arr_[hash_size_] = {};
};

}  // namespace firebuild
#endif  // FIREBUILD_HASH_H_
