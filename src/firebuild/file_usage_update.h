/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef FIREBUILD_FILE_USAGE_UPDATE_H_
#define FIREBUILD_FILE_USAGE_UPDATE_H_

#include <string>

#include "firebuild/file_info.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

class FileUsageUpdate {
 public:
  FileUsageUpdate(const FileName *filename, FileInfo info, bool written = false,
                  bool mode_changed = false)
      : initial_state_(info), filename_(filename), written_(written), mode_changed_(mode_changed),
        generation_(filename->generation()) {}

  explicit FileUsageUpdate(const FileName *filename, FileType type = DONTKNOW, bool written = false,
                           bool mode_changed = false)
      : initial_state_(type), filename_(filename), written_(written), mode_changed_(mode_changed),
        generation_(filename->generation()) {}

  static FileUsageUpdate get_from_open_params(const FileName *filename, int flags,
                                              mode_t mode_with_umask, int err, bool tmp_file);
  static FileUsageUpdate get_from_mkdir_params(const FileName *filename, int err, bool tmp_dir);
  static FileUsageUpdate get_from_stat_params(const FileName *filename, mode_t mode, off_t size,
                                              int err);
  static FileUsageUpdate get_oldfile_usage_from_rename_params(const FileName* old_name,
                                                              const FileName* new_name, int err);
  static FileUsageUpdate get_newfile_usage_from_rename_params(const FileName* new_name, int err);

  FileType parent_type() const {return parent_type_;}
  bool written() const {return written_;}
  bool mode_changed() const {return mode_changed_;}
  bool tmp_file() const {return tmp_file_;}
  file_generation_t generation() const {return generation_;}
  bool unknown_err() const {return unknown_err_;}

  bool get_initial_type(FileType *type_ptr) const;
  void set_initial_type(FileType type) {initial_state_.set_type(type);}
  bool initial_size_known() const {return initial_state_.size_known();}
  size_t initial_size() const {return initial_state_.size();}
  void set_initial_size(size_t size) {initial_state_.set_size(size);}
  bool initial_hash_known() const {return initial_state_.hash_known() || hash_computer_ != nullptr;}
  bool get_initial_hash(Hash *hash_ptr) const;
  void set_initial_hash(const Hash& hash) {initial_state_.set_hash(hash);}
  void set_initial_mode_bits(mode_t mode, mode_t mode_mask)
      {initial_state_.set_mode_bits(mode, mode_mask);}
  mode_t initial_mode() const {return initial_state_.mode();}
  mode_t initial_mode_mask() const {return initial_state_.mode_mask();}
  const FileInfo& initial_state() const {return initial_state_;}

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

  /* Whenever hash computation takes place, this is the maximum number of allowed writers.
   * E.g. if a file is opened for reading then this number is 0 (meaning no writers allowed at all),
   * but when a file is opened for writing then this number is 1 (because the intercepted process
   * has just opened it for writing, but there must not be any other writers.) */
  int max_writers_ {0};

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

  /** The file's mode was altered by the process.
   *  (Luckily for us there's no way to set individual bits, chmod() always sets all of them.
   *  So a single boolean can refer to all the 12 mode bits.) */
  bool mode_changed_ {false};

  /** Created as a temporary file with mktemp() and friends or inferred to be a temporary file
   *  by the supervisor. */
  bool tmp_file_ {false};

  /* File's current generation. */
  file_generation_t generation_;

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
