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

#include <tsl/hopscotch_map.h>

#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/execed_process.h"
#include "common/firebuild_common.h"

namespace firebuild {

std::unordered_set<FileName, FileNameHasher>* FileName::db_;
tsl::hopscotch_map<const FileName*, XXH128_hash_t>* FileName::hash_db_;
tsl::hopscotch_map<const FileName*, std::pair<int, ExecedProcess*>>* FileName::write_ofds_db_;
tsl::hopscotch_map<const FileName*, file_generation_t>* FileName::generation_db_;

const FileName* FileName::default_tmpdir;

FileName::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileName, FileNameHasher>();
  hash_db_ = new tsl::hopscotch_map<const FileName*, XXH128_hash_t>();
  write_ofds_db_ = new tsl::hopscotch_map<const FileName*, std::pair<int, ExecedProcess*>>();
  generation_db_ = new tsl::hopscotch_map<const FileName*, file_generation_t>();
}

bool FileName::isDbEmpty() {
  return !db_ || db_->empty();
}

FileName::DbInitializer FileName::db_initializer_;

void FileName::open_for_writing(ExecedProcess* proc) const {
  TRACKX(FB_DEBUG_FS, 1, 0, FileName, this, "proc=%s", D(proc));
  if (is_in_ignore_location()) {
    /* Ignored locations can be ignored here, too. */
    return;
  }
  assert(proc);
  auto it = write_ofds_db_->find(this);
  if (it != write_ofds_db_->end()) {
    auto& pair = it.value();
    assert(pair.first > 0);
    pair.first++;
    if (proc != pair.second && this != proc->jobserver_fifo()) {
      /* A different process opened the file for writing. */
      ExecedProcess* common_ancestor =
          proc->common_exec_ancestor(pair.second);
      const ExecedProcess* other_proc = pair.second;
      if (common_ancestor != proc) {
        proc->disable_shortcutting_bubble_up_to_excl(
            common_ancestor, deduplicated_string(
                "Opened " + this->to_string()
                + " for writing which file is already opened for writing by ["
                + d(other_proc->pid()) + "] \"" +  other_proc->args_to_short_string()
                + "\"").c_str());
      }
      if (common_ancestor != pair.second) {
        pair.second->disable_shortcutting_bubble_up_to_excl(
            common_ancestor, deduplicated_string(
                "An other process opened " + this->to_string()
                + " for writing which file is already opened for writing by ["
                + d(other_proc->pid()) + "] \"" +  other_proc->args_to_short_string()
                + "\"").c_str());
        pair.second = common_ancestor;
      }
    }
  } else {
    write_ofds_db_->insert({this, {1, proc}});
    auto it2 = generation_db_->find(this);
    if (it2 != generation_db_->end()) {
      assert(it2->second < UINT32_MAX);
      it2.value()++;
      /* Bubble up the generation change */
      proc->register_file_usage_update(this, FileUsageUpdate(this));
    } else {
      generation_db_->insert({this, 1});
    }
  }
}

void FileName::close_for_writing() const {
  TRACKX(FB_DEBUG_FS, 1, 0, FileName, this, "");
  if (is_in_ignore_location()) {
    /* Ignored locations can be ignored here, too. */
    return;
  }
  auto it = write_ofds_db_->find(this);
  assert(it != write_ofds_db_->end());
  assert(it->second.first > 0);
  if (it->second.first > 1) {
    it.value().first--;
  } else {
    write_ofds_db_->erase(it);
  }
}

const FileName* FileName::GetParentDir(const char * const name, ssize_t length) {
  /* name is canonicalized, so just simply strip the last component */
  ssize_t slash_pos = length - 1;
  for (; slash_pos >= 0; slash_pos--) {
    if (name[slash_pos] == '/') {
      break;
    }
  }

  /* A path that does not have a '/' in it or "/" itself does not have a parent */
  if (slash_pos == -1 || length == 1) {
    return nullptr;
  }

  if (slash_pos == 0) {
    /* Path is in the "/" dir. */
    return Get("/", 1);
  } else {
    char* parent_name = reinterpret_cast<char*>(alloca(slash_pos + 1));
    memcpy(parent_name, name, slash_pos);
    parent_name[slash_pos] = '\0';
    return Get(parent_name, slash_pos);
  }
}

bool FileName::is_at_locations(const cstring_view_array* locations) const {
  return is_path_at_locations(this->name_, this->length_, locations);
}

const FileName* FileName::GetCanonicalized(const char * name, size_t length,
                                           const FileName* wd) {
  assert(wd);
  char* canonicalized_name = const_cast<char*>(name);
  size_t canonical_len = length;
  if (!is_canonical(name, length)) {
    canonicalized_name = static_cast<char*>(alloca(length + 1));
    memcpy(canonicalized_name, name, length + 1);
    canonical_len = make_canonical(canonicalized_name, length);
  }
  if (path_is_absolute(canonicalized_name)) {
    return Get(canonicalized_name, canonical_len);
  } else {
    char* buf = reinterpret_cast<char*>(alloca(wd->length() + 1 + canonical_len + 1));
    memcpy(buf, wd->c_str(), wd->length());
    size_t offset = wd->length();
    if (wd->c_str()[wd->length() - 1] != '/') {
      buf[offset++] = '/';
    }
    memcpy(buf + offset, canonicalized_name, canonical_len + 1);
    return Get(buf, offset + canonical_len);
  }
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileName& fn, const int level) {
  (void)level;  /* unused */
  return d(fn.to_string());
}
std::string d(const FileName *fn, const int level) {
  if (fn) {
    return d(*fn, level);
  } else {
    return "{FileName NULL}";
  }
}

}  /* namespace firebuild */
