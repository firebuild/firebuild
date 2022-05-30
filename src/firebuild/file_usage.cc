
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * FileUsage describes, for one particular Process and one particular
 * filename, the inital and final contents found at the given location
 * with as much accuracy as it matters to us.
 *
 * E.g. if the Process potentially reads from the file then its original
 * hash is computed and stored here, but if the Process does not read
 * the contents then it is not stored. Similarly, it's recorded whether
 * the process potentially modified the file.
 *
 * All these objects are kept in a global pool. If two such objects have
 * identical contents then they are the same object (same pointer).
 */

#include "firebuild/file_usage.h"

#include <sys/stat.h>

#include <string>
#include <unordered_set>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/hash.h"

namespace firebuild {

std::unordered_set<FileUsage, FileUsageHasher>* FileUsage::db_;
const FileUsage* FileUsage::no_hash_not_written_states_[FILE_TYPE_MAX + 1];

FileUsage::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileUsage, FileUsageHasher>();
  for (int i = 0; i <= FILE_TYPE_MAX; i++) {
    const FileUsage fu(FileInfo::int_to_file_type(i));
    no_hash_not_written_states_[i] = &*db_->insert(fu).first;
  }
}

FileUsage::DbInitializer FileUsage::db_initializer_;

bool operator==(const FileUsage& lhs, const FileUsage& rhs) {
  return lhs.initial_state_ == rhs.initial_state_ &&
      lhs.written_ == rhs.written_ &&
      lhs.mode_changed_ == rhs.mode_changed_ &&
      lhs.generation_ == rhs.generation_ &&
      lhs.unknown_err_ == rhs.unknown_err_;
}

const FileUsage* FileUsage::Get(const FileUsage& candidate) {
  auto it = db_->find(candidate);
  if (it != db_->end()) {
    return &*it;
  } else {
    /* Not found, add a copy to the set. */
    return &*db_->insert(candidate).first;
  }
}

/**
 * Merge a FileUsageUpdate object into this one.
 *
 * "this" describes the older events which happened to a file, and "update" describes the new ones.
 *
 * "this" is not updated, a possibly different pointer is returned which refers to the merged value.
 *
 * "update" might on demand compute certain values (currently the hash). It's formally "const", but
 * with some "mutable" members. The value behind the "update" reference is updated, so when this
 * change is bubbled up, at the next levels it'll have this field already filled in.
 *
 * Sometimes the file usages to merge are conflicting, like a directory was expected to not exist,
 * then it is expected to exist without creating it in the meantime. In those cases the return is
 * nullptr and it should disable shortcutting of the process and its ancestors.
 *
 * @return pointer to the merge result, or nullptr in case of an error
 */
const FileUsage *FileUsage::merge(const FileUsageUpdate& update) const {
  FileUsage tmp = *this;

  /* Make sure the merged FileUsage is debug-printed upon leaving this method. */
#ifdef FB_EXTRA_DEBUG
  const FileUsage *fu = &tmp;
#endif
  TRACKX(FB_DEBUG_PROC, 1, 1, FileUsage, fu, "other=%s", D(update));

  bool changed = false;

  if (generation() != update.generation()) {
    assert(generation() < update.generation());
    tmp.generation_ = update.generation();
    changed = true;
  }

  if (!written_) {
    /* Note: this might lazily query the type now. Avoid calling it multiple times. */
    FileType update_initial_type;
    if (!update.get_initial_type(&update_initial_type)) {
      return nullptr;
    }

    switch (initial_type()) {
      case DONTKNOW: {
        if (initial_type() != update_initial_type) {
          tmp.set_initial_type(update_initial_type);
          changed = true;
        }
        if (!initial_size_known() && update.initial_size_known()) {
          tmp.set_initial_size(update.initial_size());
          changed = true;
        }
        if (!initial_hash_known() && update.initial_hash_known()) {
          Hash hash;
          /* Note: this might lazily compute the hash now. */
          if (!update.get_initial_hash(&hash)) {
            return nullptr;
          }
          tmp.set_initial_hash(hash);
          changed = true;
        }
        break;
      }
      case EXIST: {
        if (update_initial_type == NOTEXIST) {
          return nullptr;
        } else if (update_initial_type == NOTEXIST_OR_ISREG) {
          /* We knew from an access() that it existed, now we got to know from an open() that it
           * either didn't exist or was a regular file. That is: it was a regular file. */
          tmp.set_initial_type(ISREG);
          if (update.initial_size_known()) {
            assert(update.initial_size() == 0);
            tmp.set_initial_size(update.initial_size());
          }
          changed = true;
        } else {
          /* Copy over the new values */
          // FIXME This is copied from the DONTKNOW case, maybe factor out to a helper method.
          if (initial_type() != update_initial_type) {
            tmp.set_initial_type(update_initial_type);
            changed = true;
          }
          if (!initial_size_known() && update.initial_size_known()) {
            tmp.set_initial_size(update.initial_size());
            changed = true;
          }
          if (!initial_hash_known() && update.initial_hash_known()) {
            Hash hash;
            /* Note: this might lazily compute the hash now. */
            if (!update.get_initial_hash(&hash)) {
              return nullptr;
            }
            tmp.set_initial_hash(hash);
            changed = true;
          }
        }
        break;
      }
      case NOTEXIST: {
        if (update_initial_type != DONTKNOW &&
            update_initial_type != NOTEXIST &&
            update_initial_type != NOTEXIST_OR_ISREG) {
          return nullptr;
        }
        break;
      }
      case NOTEXIST_OR_ISREG: {
        /* This initial state, without the written_ bit, is possible intermittently while
         * shortcutting a process. See #791. */
        break;
      }
      case ISREG: {
        if (update_initial_type != DONTKNOW &&
            update_initial_type != EXIST &&
            update_initial_type != NOTEXIST_OR_ISREG &&
            update_initial_type != ISREG) {
          return nullptr;
        }
        if (!initial_size_known() && update.initial_size_known()) {
          /* Note this might lazily figure out the size now. */
          tmp.set_initial_size(update.initial_size());
          changed = true;
        }
        if (!initial_hash_known() && update.initial_hash_known()) {
          Hash hash;
          /* Note: this might lazily compute the hash now. */
          if (!update.get_initial_hash(&hash)) {
            return nullptr;
          }
          tmp.set_initial_hash(hash);
          changed = true;
        }
        break;
      }
      case ISDIR: {
        if (update_initial_type != DONTKNOW &&
            update_initial_type != EXIST &&
            update_initial_type != ISDIR) {
          return nullptr;
        }
        if (!initial_hash_known() && update.initial_hash_known()) {
          Hash hash;
          /* Note: this might lazily compute the hash now. */
          if (!update.get_initial_hash(&hash)) {
            return nullptr;
          }
          tmp.set_initial_hash(hash);
          changed = true;
        }
        break;
      }
    }

    if (update.written()) {
      tmp.written_ = true;
      changed = true;
    }
  }

  if (!mode_changed_) {
    // FIXME this condition could be even more fine-grained to detect if things won't change
    if (initial_mode() != update.initial_mode() ||
        initial_mode_mask() != update.initial_mode_mask()) {
      tmp.set_initial_mode_bits(update.initial_mode(), update.initial_mode_mask());
      changed = true;
    }

    if (update.mode_changed()) {
      tmp.mode_changed_ = true;
      changed = true;
    }
  }

  if (!changed) {
    return this;
  } else {
    return FileUsage::Get(tmp);
  }
}

bool file_file_usage_cmp(const file_file_usage& lhs, const file_file_usage& rhs) {
  return strcmp(lhs.file->c_str(), rhs.file->c_str()) < 0;
}

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string FileUsage::d_internal(const int level) const {
  (void)level;  /* unused */
  return std::string("{FileUsage initial_state=") + d(initial_state_, level) +
      ", written=" + d(written_) +
      ", mode_changed=" + d(mode_changed_) +
      ", generation=" + d(generation_) + "}";
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileUsage& fu, const int level) {
  return fu.d_internal(level);
}
std::string d(const FileUsage *fu, const int level) {
  if (fu) {
    return d(*fu, level);
  } else {
    return "{FileUsage NULL}";
  }
}

}  /* namespace firebuild */
