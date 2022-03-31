/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_INFO_H_
#define FIREBUILD_FILE_INFO_H_

#include <string>

#include "firebuild/hash.h"

namespace firebuild {

typedef enum {
  /** We don't have any information about this file yet (used only temporarily
   *  while building up our data structures). */
  DONTKNOW,
  /** We know and care that the file or directory did not exist before (e.g. an
   *  open(O_CREAT|O_WRONLY|O_EXCL) or a mkdir() succeeded). */
  NOTEXIST,
  /** We know and care that the file either did not exist before, or was
   *  a zero sized regular file (but we do not know which of these, because
   *  an open(O_CREAT|O_WRONLY) succeeded and resulted in a zero length file). */
  NOTEXIST_OR_ISREG_EMPTY,
  /** We know and care that the file either did not exist before, or was a
   *  regular file (e.g. a creat() a.k.a. open(O_CREAT|O_WRONLY|O_TRUNC) succeeded). */
  NOTEXIST_OR_ISREG,
  /** We know and care that the regular file existed before.
   *  (Maybe we don't know more, e.g. an open(O_WRONLY|O_TRUNC) succeeded.
   *  Maybe we know the size, e.g. a stat() succeeded.
   *  Maybe we know the size and the checksum, e.g. an open() succeeded for reading,
   *  or for writing without truncating.) */
  ISREG,
  /** We know and care that the directory existed before.
   *  (Maybe we don't know more, e.g. a stat() succeeded.
   *  Maybe we know the listing's checksum, e.g. an opendir() was performed.) */
  ISDIR,
  FILE_TYPE_MAX = ISDIR
} FileType;

class FileInfo {
 public:
  explicit FileInfo(FileType type = DONTKNOW, ssize_t size = -1, const Hash *hash = nullptr) :
      type_(type),
      size_(size),
      hash_known_(hash != nullptr),
      hash_(hash != nullptr ? *hash : Hash()) {
    assert(type == ISREG || type == ISDIR || hash == nullptr);
  }

  FileType type() const {return type_;}
  void set_type(FileType type) {type_ = type;}
  bool size_known() const {return size_ >= 0;}
  ssize_t size() const {return size_;}
  void set_size(ssize_t size) {
    size_ = size;
  }
  bool hash_known() const {return hash_known_;}
  const Hash& hash() const {return hash_;}
  void set_hash(const Hash& hash) {
    hash_ = hash;
    hash_known_ = true;
  }

  /* Misc */
  static int file_type_to_int(const FileType t) {
    switch (t) {
      case DONTKNOW: return DONTKNOW;
      case NOTEXIST: return NOTEXIST;
      case NOTEXIST_OR_ISREG_EMPTY: return NOTEXIST_OR_ISREG_EMPTY;
      case NOTEXIST_OR_ISREG: return NOTEXIST_OR_ISREG;
      case ISREG: return ISREG;
      case ISDIR: return ISDIR;
      default:
        abort();
    }
  }

  static FileType int_to_file_type(const int t) {
    switch (t) {
      case DONTKNOW: return DONTKNOW;
      case NOTEXIST: return NOTEXIST;
      case NOTEXIST_OR_ISREG_EMPTY: return NOTEXIST_OR_ISREG_EMPTY;
      case NOTEXIST_OR_ISREG: return NOTEXIST_OR_ISREG;
      case ISREG: return ISREG;
      case ISDIR: return ISDIR;
      default:
        abort();
    }
  }

 private:
  /** File type. */
  FileType type_;

  /** The size, if known. Only if type_ is ISREG. For ISREG, if the checksum is known then the size
   *  is also known. If the size is not known or is irrelevant (type_ isn't ISREG) then -1. */
  ssize_t size_;

  /** Whether the checksum is known. Only if type_ is ISREG or ISDIR.
   *  For regular files, knowing the checksum implies we know the size, too. */
  // FIXME(egmont) Do we want to have special treatment for zero-length files,
  // either always set the hash (copy from a global variable), or never set it?
  bool hash_known_ : 1;

  /** The checksum, if known. Only if type_ is ISREG or ISDIR.
   *  For directories, it's the checksum of its listing. */
  Hash hash_;

  friend bool operator==(const FileInfo& lhs, const FileInfo& rhs);
};

bool operator==(const FileInfo& lhs, const FileInfo& rhs);

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileInfo& fi, const int level = 0);
std::string d(const FileInfo *fi, const int level = 0);
const char *file_type_to_string(FileType type);

}  /* namespace firebuild */

#endif  // FIREBUILD_FILE_INFO_H_
