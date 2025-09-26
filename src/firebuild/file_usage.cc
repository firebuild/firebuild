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

#include "firebuild/file_usage.h"

#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <unordered_set>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/hash.h"

namespace firebuild {

std::unordered_set<FileUsage, FileUsageHasher>* FileUsage::db_;
const FileUsage* FileUsage::no_hash_not_written_states_[FILE_TYPE_MAX + 1];

FileUsage::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileUsage, FileUsageHasher>(8192);
  for (int i = 0; i <= FILE_TYPE_MAX; i++) {
    const FileUsage fu(FileInfo::int_to_file_type(i));
    no_hash_not_written_states_[i] = &*db_->insert(fu).first;
  }
}

FileUsage::DbInitializer FileUsage::db_initializer_;

const FileUsage* FileUsage::Get(const FileUsage& candidate) {
  auto it = db_->find(candidate);
  if (it != db_->end()) {
    return &*it;
  } else {
    /* Not found, add a copy to the set. */
    return &*db_->insert(candidate).first;
  }
}

const FileUsage* FileUsage::merge(const FileUsageUpdate& update, const bool propagated) const {
  FileUsage tmp = *this;

  /* Make sure the merged FileUsage is debug-printed upon leaving this method. */
#ifdef FB_EXTRA_DEBUG
  const FileUsage *fu = &tmp;
#endif
  TRACKX(FB_DEBUG_PROC, 1, 1, FileUsage, fu, "other=%s", D(update));

  bool changed = false;

  if (generation() != update.generation()) {
    /* Ensured by the caller. */
    assert((generation() == 0 && initial_type() == DONTKNOW)
           || generation() + 1 == update.generation());
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

  if (!tmp_file_) {
    if (update.tmp_file()) {
      tmp.tmp_file_ = true;
      changed = true;
    }
  }

  if (propagated_ != propagated) {
    tmp.propagated_ = propagated;
    changed = true;
  }

  if (!changed) {
    return this;
  } else {
    return FileUsage::Get(tmp);
  }
}

bool file_file_usage_cmp(const file_file_usage& lhs, const file_file_usage& rhs) {
  return memcmp(lhs.file->c_str(), rhs.file->c_str(),
                std::min(lhs.file->length(), rhs.file->length()) + 1) < 0;
}

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
