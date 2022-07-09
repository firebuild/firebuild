/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_USAGE_H_
#define FIREBUILD_FILE_USAGE_H_

#include <sys/stat.h>
#include <xxhash.h>

#include <string>
#include <unordered_set>

#include "firebuild/file_info.h"
#include "firebuild/file_usage_update.h"
#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

struct FileUsageHasher;

class FileUsage {
 public:
  bool written() const {return written_;}
  bool mode_changed() const {return mode_changed_;}
  file_generation_t generation() const {return generation_;}
  int unknown_err() {return unknown_err_;}
  void set_unknown_err(int e) {unknown_err_ = e;}

  FileType initial_type() const {return initial_state_.type();}
  void set_initial_type(FileType type) {initial_state_.set_type(type);}
  bool initial_size_known() const {return initial_state_.size_known();}
  size_t initial_size() const {return initial_state_.size();}
  void set_initial_size(size_t size) {initial_state_.set_size(size);}
  bool initial_hash_known() const {return initial_state_.hash_known();}
  const Hash& initial_hash() const {return initial_state_.hash();}
  void set_initial_hash(const Hash& hash) {initial_state_.set_hash(hash);}
  void set_initial_mode_bits(mode_t mode, mode_t mode_mask)
      {initial_state_.set_mode_bits(mode, mode_mask);}
  mode_t initial_mode() const {return initial_state_.mode();}
  mode_t initial_mode_mask() const {return initial_state_.mode_mask();}
  const FileInfo& initial_state() const {return initial_state_;}

  static const FileUsage* Get(FileType type = DONTKNOW) {
    return no_hash_not_written_states_[FileInfo::file_type_to_int(type)];
  }

  const FileUsage *merge(const FileUsageUpdate& update) const;

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  std::string d_internal(const int level = 0) const;

 private:
  explicit FileUsage(FileType type = DONTKNOW) : initial_state_(type) {}

  FileUsage(const FileName* filename, const FileInfo *initial_state, bool written,
            bool mode_changed, int unknown_err):
      initial_state_(*initial_state), written_(written), mode_changed_(mode_changed),
      generation_(filename->generation()), unknown_err_(unknown_err) {}

  /* Things that describe the filesystem when the process started up */
  FileInfo initial_state_;

  /* Things that describe what the process potentially did */

  /** The file's contents were altered by the process, e.g. written to,
   *  or modified in any other way, including removal of the file, or
   *  another file getting renamed to this one. */
  bool written_ {false};

  /** The file's mode was altered by the process.
   *  (Luckily for us there's no way to set individual bits, chmod() always sets all of them.
   *  So a single boolean can refer to all the 12 mode bits.) */
  bool mode_changed_ {false};

  /** Generation of the file the process last seen (either by reading or writing to the file). */
  file_generation_t generation_ {0};

  /* Note: stuff like the final hash are not stored here. They are
   * computed right before being placed in the cache, don't need to be
   * remembered in memory. */

  /** Global FileUsage db*/
  static std::unordered_set<FileUsage, FileUsageHasher>* db_;
  /** Frequently used singletons */
  static const FileUsage* no_hash_not_written_states_[FILE_TYPE_MAX + 1];

  /* This, along with the FileUsage::db_initializer_ definition in file_usage.cc,
   * initializes the file usage database once at startup. */
  class DbInitializer {
   public:
    DbInitializer();
  };
  friend class DbInitializer;
  static DbInitializer db_initializer_;
  friend struct FileUsageHasher;
  friend bool operator==(const FileUsage& lhs, const FileUsage& rhs) = default;

  /** An unhandled error occurred during operation on the file. The process
   *  can't be short-cut, but the first such error code is stored here. */
  int unknown_err_ {0};

  static const FileUsage* Get(const FileUsage& candidate);
};

struct FileUsageHasher {
  std::size_t operator()(const FileUsage& f) const noexcept {
    XXH64_hash_t hash = XXH3_64bits_withSeed(f.initial_hash().get_ptr(), Hash::hash_size(),
                                             f.unknown_err_);
    ssize_t size = f.initial_size();
    hash = XXH3_64bits_withSeed(&size, sizeof(size), hash);
    struct {
      uint64_t initial_type : 3;
      uint64_t initial_mode : 12;
      uint64_t initial_mode_mask : 12;
      uint64_t written : 1;
      uint64_t mode_changed : 1;
      uint64_t unused : 3;  /* 32 bits so far */
      uint64_t generation : 32;
    } merged_state = {f.initial_type(), f.initial_mode(), f.initial_mode_mask(),
                      f.written_, f.mode_changed_, 0, f.generation_};
    hash = XXH3_64bits_withSeed(&merged_state, sizeof(merged_state), hash);
    return hash;
  }
};


struct file_file_usage {
  const FileName* file;
  const FileUsage* usage;
};

bool file_file_usage_cmp(const file_file_usage& lhs, const file_file_usage& rhs);

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileUsage& fu, const int level = 0);
std::string d(const FileUsage *fu, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_FILE_USAGE_H_
