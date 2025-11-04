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

#ifndef FIREBUILD_FILE_NAME_H_
#define FIREBUILD_FILE_NAME_H_

#include <tsl/hopscotch_map.h>
#include <xxhash.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/debug.h"

namespace firebuild {

class ExecedProcess;

typedef uint32_t file_generation_t;

struct FileNameHasher;
class FileName {
 public:
  FileName(const FileName& other)
      : name_(reinterpret_cast<const char *>(malloc(other.length_ + 1))),
        length_(other.length_), in_ignore_location_(other.in_ignore_location_),
        in_read_only_location_(other.in_read_only_location_) {
    memcpy(const_cast<char*>(name_), other.name_, other.length_ + 1);
  }
  const char * c_str() const {return name_;}
  std::string to_string() const {return std::string(name_);}
  uint32_t length() const {return length_;}
  const FileName* parent_dir() const {return GetParentDir(name_, length_);}
  size_t hash() const {return XXH3_64bits(name_, length_);}
  const XXH128_hash_t& hash_XXH128() const {
    auto it = hash_db_->find(this);
    if (it != hash_db_->end()) {
      return it->second;
    } else {
      /* Not found, add a copy to the set. */
      return (hash_db_->insert({this,  XXH3_128bits(name_, length_)}).first)->second;
    }
  }
  int writers_count() const {
    /* Files in ignored locations should not even be queried. */
    assert(!is_in_ignore_location());
    auto it = write_ofds_db_->find(this);
    if (it != write_ofds_db_->end()) {
      assert(it->second.first > 0);
      return it->second.first;
    } else {
      return 0;
    }
  }
  void open_for_writing(ExecedProcess* proc) const;
  void close_for_writing() const;
  file_generation_t generation() const {
    auto it = generation_db_->find(this);
    if (it != generation_db_->end()) {
      assert(it->second > 0);
      return it->second;
    } else {
      return 0;
    }
  }
  static bool isDbEmpty();
  static const FileName* Get(const char * const name, ssize_t length);
  static const FileName* Get(const std::string& name) {
    return Get(name.c_str(), name.size());
  }
  static const FileName* GetCanonicalized(const char * name, size_t length,
                                          const FileName* wd);
  /**
   * Return parent dir or nullptr for "/"
   */
  static const FileName* GetParentDir(const char * const name, ssize_t length);

  bool is_in_ignore_location() const {return in_ignore_location_;}
  bool is_in_read_only_location() const {return in_read_only_location_;}

  std::string without_dirs() const {
    // TODO(rbalint) use std::string::ends_with when we switch to c++20
    const char* last_slash = strrchr(name_, '/');
    if (last_slash) {
      return std::string(last_slash + 1);
    } else {
      return to_string();
    }
  }
  static const FileName* default_tmpdir;

 private:
  FileName(const char * const name, size_t length, bool copy_name)
      : name_(copy_name ? reinterpret_cast<const char *>(malloc(length + 1)) : name),
        length_(length) {
    if (copy_name) {
      memcpy(const_cast<char*>(name_), name, length);
      const_cast<char*>(name_)[length] = '\0';
    }
  }

  /**
   * Checks if a path semantically begins with one of the given sorted subpaths.
   *
   * Does string operations only, does not look at the file system.
   */
  bool is_at_locations(const cstring_view_array *locations) const;

  const char * const name_;
  const uint32_t length_;
  const bool in_ignore_location_ = false;
  const bool in_read_only_location_ = false;
  static std::unordered_set<FileName, FileNameHasher>* db_;
  static tsl::hopscotch_map<const FileName*, XXH128_hash_t>* hash_db_;
  /** Number of FileOFDs open for writing referencing this file. */
  static tsl::hopscotch_map<const FileName*, std::pair<int, ExecedProcess*>>* write_ofds_db_;
  /**
   * A generation of the file is when it is kept open by a set of writers.
   * Whenever all writers close the file and thus the refcount in write_ofds_db_ decreases to zero
   * the generation is closed, but the generation number stays the same. When the new writer opens
   * the file a new generation is opened.
   * A file's generation number is 0 until it is opened for writing for the first time.
   */
  static tsl::hopscotch_map<const FileName*, file_generation_t>* generation_db_;

  /* Disable assignment. */
  void operator=(const FileName&);

  /* This, along with the FileName::db_initializer_ definition in file_namedb.cc,
   * initializes the filename database once at startup. */
  class DbInitializer {
   public:
    DbInitializer();
  };
  friend class DbInitializer;
  static DbInitializer db_initializer_;
};

inline bool operator==(const FileName& lhs, const FileName& rhs) {
  return lhs.length() == rhs.length() && memcmp(lhs.c_str(), rhs.c_str(), lhs.length()) == 0;
}

struct FileNameHasher {
  std::size_t operator()(const FileName& s) const noexcept {
    return s.hash();
  }
};

/** Helper struct for std::sort */
struct FileNameLess {
  bool operator()(const FileName* f1, const FileName* f2) const {
    return memcmp(f1->c_str(), f2->c_str(), std::min(f1->length(), f2->length()) + 1) < 0;
  }
};

extern cstring_view_array ignore_locations;
extern cstring_view_array read_only_locations;

inline const FileName* FileName::Get(const char * const name, ssize_t length) {
  FileName tmp_file_name(name, (length == -1) ? strlen(name) : length, false);
#ifdef FB_EXTRA_DEBUG
  assert(is_canonical(tmp_file_name.name_, tmp_file_name.length_));
#endif
  auto it = db_->find(tmp_file_name);
  if (it != db_->end()) {
    return &*it;
  } else {
    *const_cast<bool*>(&tmp_file_name.in_ignore_location_) =
        tmp_file_name.is_at_locations(&ignore_locations);
    *const_cast<bool*>(&tmp_file_name.in_read_only_location_) =
        tmp_file_name.is_at_locations(&read_only_locations);
    /* Not found, add a copy to the set. */
    return &*db_->insert(tmp_file_name).first;
  }
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileName& fn, const int level = 0);
std::string d(const FileName *fn, const int level = 0);

}  /* namespace firebuild */

#endif  // FIREBUILD_FILE_NAME_H_
