/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_USAGE_UPDATE_H_
#define FIREBUILD_FILE_USAGE_UPDATE_H_

#include <string>

#include "firebuild/file_info.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

class FileUsageUpdate {
 public:
  FileUsageUpdate(const FileName *filename, FileInfo info, bool written = false)
      : initial_state_(info), filename_(filename), written_(written) {}

  explicit FileUsageUpdate(const FileName *filename, FileType type = DONTKNOW, bool written = false)
      : initial_state_(type), filename_(filename), written_(written) {}

  static FileUsageUpdate get_from_open_params(const FileName *filename, int flags, int err);
  static FileUsageUpdate get_from_mkdir_params(const FileName *filename, int err);
  static FileUsageUpdate get_from_stat_params(const FileName *filename, mode_t mode, int err);

  bool get_initial_type(FileType *type_ptr) const;
  void set_initial_type(FileType type) {initial_state_.set_type(type);}
  bool initial_size_known() const {return initial_state_.size_known();}
  size_t initial_size() const {return initial_state_.size();}
  void set_initial_size(size_t size) {initial_state_.set_size(size);}
  bool initial_hash_known() const {return initial_state_.hash_known() || hash_computer_ != nullptr;}
  bool get_initial_hash(Hash *hash_ptr) const;
  void set_initial_hash(const Hash& hash) {initial_state_.set_hash(hash);}

  FileType parent_type() const {return parent_type_;}
  bool written() const {return written_;}
  bool unknown_err() const {return unknown_err_;}

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  std::string d_internal(const int level = 0) const;

 private:
  /* The information we got to know about the file, prior to the changes that potentially occurred
   * to it.
   *
   * If type_computer_ is set then initial_state_.type_ is not yet set to its correct value, it'll
   * be figured out on demand.
   *
   * If hash_computer_ is set then initial_state_.hash_ is not yet set to its correct value, it'll
   * be figured out on demand. However, initial_state_.hash_known() reports true. */
  mutable FileInfo initial_state_;

  /* The filename, used when needed to lazily initialize some fields. */
  const FileName *filename_;

  void type_computer_open_rdonly() const;
  void type_computer_open_wronly_creat_notrunc_noexcl() const;
  void hash_computer() const;

  /* If initial_state_'s type_ or hash_ aren't known yet (but in case of hash_ we know that we'll
   * need to know it), they will be initialized on demand by these methods. */
  mutable
      void (FileUsageUpdate::*type_computer_)() const {nullptr};  /* NOLINT(readability/braces) */
  mutable
      void (FileUsageUpdate::*hash_computer_)() const {nullptr};  /* NOLINT(readability/braces) */

  /* The file's contents were altered by the process, e.g. written to, or modified in any other way,
   * including removal of the file, or another file getting renamed to this one. */
  bool written_ {false};

  /* What we know and are interested in about the parent path. E.g.
   * - DONTKNOW = nothing of interest
   * - NOTEXIST = no such entry on the filesystem
   * - ISDIR = it is a directory
   * - ISREG = it is a regular file */
  FileType parent_type_ {DONTKNOW};

  /* This does not strictly obey the semantics of "mutable" because lazy evaluation of
   * initial_state_.type_ or initial_state_.hash_ might modify it, but I don't think it'll be a
   * problem, since we don't query this variable before performing the lazy evaluation. */
  mutable int unknown_err_ {0};
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileUsageUpdate& fuu, const int level = 0);
std::string d(const FileUsageUpdate *fuu, const int level = 0);

}  /* namespace firebuild */

#endif  // FIREBUILD_FILE_USAGE_UPDATE_H_
