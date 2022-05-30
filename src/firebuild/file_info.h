/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_INFO_H_
#define FIREBUILD_FILE_INFO_H_

#include <string>

#include "firebuild/hash.h"

namespace firebuild {

typedef enum {
  /** No information about the filesystem entry (its presence/absence, type, etc.). */
  DONTKNOW,
  /** We know that the filesystem entry exists, but don't know if it's a regular file or a
   *  directory. This happens at a successful access(F_OK). */
  EXIST,
  /** We know that the filesystem entry doesn't exist. We might know it by a failed access(F_OK)
   *  or stat(). We also might know it about the initial state of the filesystem entry, if later an
   *  open(O_CREAT|O_WRONLY|O_EXCL) or mkdir() succeeds. */
  NOTEXIST,
  /** We know that the filesystem entry either does not exist, or is a regular file, but we don't
   *  know which. We might know it about the initial state of a file, if later a creat() a.k.a.
   *  open(O_CREAT|O_WRONLY|O_TRUNC) succeeds, or an open(O_CREAT|O_WRONLY) succeeds and results in
   *  a zero length file. In the latter case, size is set to 0 in the corresponding FileInfo. */
  NOTEXIST_OR_ISREG,
  /** We know that the filesystem entry is a regular fie. */
  ISREG,
  /** We know that the filesystem entry is a directory. */
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
  void set_hash(const Hash *hash) {
    if (hash) {
      hash_ = *hash;
      hash_known_ = true;
    } else {
      hash_ = Hash();
      hash_known_ = false;
    }
  }
  mode_t mode() const {return mode_;}
  mode_t mode_mask() const {return mode_mask_;}
  /* Set or clear the file mode bits where enabled by the mask, leave the other bits unchanged. */
  void set_mode_bits(mode_t mode, mode_t mask) {
    mode_ &= ~mask;
    mode_ |= (mode & mask);
    mode_mask_ |= mask;
  }

  /* Misc */
  static int file_type_to_int(const FileType t) {
    switch (t) {
      case DONTKNOW: return DONTKNOW;
      case EXIST: return EXIST;
      case NOTEXIST: return NOTEXIST;
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
      case EXIST: return EXIST;
      case NOTEXIST: return NOTEXIST;
      case NOTEXIST_OR_ISREG: return NOTEXIST_OR_ISREG;
      case ISREG: return ISREG;
      case ISDIR: return ISDIR;
      default:
        abort();
    }
  }

 private:
  /** File type.
   *
   *  If DONTKNOW or NOTEXIST then the remaining fields are meaningless and unset.
   *
   *  If NOTEXIST_OR_ISREG then the remaining fields refer to the state of the file in case it is
   *  actually a regular file (ISREG) rather than missing (NOTEXIST). */
  FileType type_;

  /** The size, if known. Only if type_ is ISREG or NOTEXIST_OR_ISREG. In these cases, if the
   *  checksum is known then the size is also known. If the size is not known or is irrelevant
   *  (type_ isn't one of these) then -1.
   *
   *  (If the type is NOTEXIST_OR_ISREG and the size is known, the size is necessarily 0. This is
   *  our knowledge about the initial / prior state of a file if open(O_CREAT|O_WRONLY) results in
   *  an empty file.) */
  ssize_t size_;

  /** Whether the checksum is known. Only if type_ is ISREG, NOTEXIST_OR_ISREG or ISDIR.
   *  For regular files, knowing the checksum implies we know the size, too.
   *
   *  (Note: currently the type cannot actually be NOTEXIST_OR_ISREG if this field is set. That's
   *  because if the type is NOTEXIST_OR_ISREG then the size, if known, is necessarily 0, and we
   *  don't fill in the checksum. As per the FIXME below, this might change in the future.) */
  //
  // FIXME(egmont) Do we want to have special treatment for zero-length files,
  // either always set the hash (copy from a global variable), or never set it?
  bool hash_known_ : 1;

  /** The checksum, if known. Only if type_ is ISREG, NOTEXIST_OR_ISREG, or ISDIR.
   *  For directories, it's the checksum of its listing.
   *
   *  (Note: currently the type cannot actually be NOTEXIST_OR_ISREG if this field is set. That's
   *  because if the type is NOTEXIST_OR_ISREG then the size, if known, is necessarily 0, and we
   *  don't fill in the checksum. As per the FIXME below, this might change in the future.) */
  //
  // FIXME(egmont) Do we want to have special treatment for zero-length files,
  // either always set the hash (copy from a global variable), or never set it?
  Hash hash_;

  /** The mode of the file, i.e. the 12 bits: setuid, setgid, sticky, owner-readable, etc.
   *  If the corresponding bit in mode_mask_ is set then the mode is known to
   *  have the given property (set or unset) as contained in this mode_ here.
   *  If the corresponding bit in mode_mask_ is unset then that bit here is zero (unused). */
  mode_t mode_ {0};

  /** Which of the bits in mode_ are known. */
  mode_t mode_mask_ {0};

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
