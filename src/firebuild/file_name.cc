/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <tsl/hopscotch_map.h>

#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/execed_process.h"

namespace firebuild {

std::unordered_set<FileName, FileNameHasher>* FileName::db_;
tsl::hopscotch_map<const FileName*, XXH128_hash_t>* FileName::hash_db_;
tsl::hopscotch_map<const FileName*, std::pair<int, Process*>>* FileName::write_fds_db_;
tsl::hopscotch_map<const FileName*, file_generation_t>* FileName::generation_db_;

const FileName* FileName::default_tmpdir;

FileName::DbInitializer::DbInitializer() {
  db_ = new std::unordered_set<FileName, FileNameHasher>();
  hash_db_ = new tsl::hopscotch_map<const FileName*, XXH128_hash_t>();
  write_fds_db_ = new tsl::hopscotch_map<const FileName*, std::pair<int, Process*>>();
  generation_db_ = new tsl::hopscotch_map<const FileName*, file_generation_t>();
}

bool FileName::isDbEmpty() {
  return !db_ || db_->empty();
}

FileName::DbInitializer FileName::db_initializer_;

void FileName::open_for_writing(Process* proc) const {
  if (is_in_ignore_location()) {
    /* Ignored locations can be ignored here, too. */
    return;
  }
  assert(proc);
  auto it = write_fds_db_->find(this);
  if (it != write_fds_db_->end()) {
    auto& pair = it.value();
    assert(pair.first > 0);
    pair.first++;
    if (proc != pair.second && proc->exec_point() != pair.second->exec_point()) {
      /* A different process opened the file for writing. */
      ExecedProcess* common_ancestor =
          proc->exec_point()->common_exec_ancestor(pair.second->exec_point());
      if (common_ancestor != proc->exec_point()) {
        proc->exec_point()->disable_shortcutting_bubble_up_to_excl(
            common_ancestor,
            "Opened a file for writing which is already opened for writing by a different process");
      }
      if (common_ancestor != pair.second->exec_point()) {
        pair.second->exec_point()->disable_shortcutting_bubble_up_to_excl(
            common_ancestor,
            "An other process opened a file for writing which is already opened for writing by "
            "this process");
      }
    }
  } else {
    write_fds_db_->insert({this, {1, proc}});
    auto it2 = generation_db_->find(this);
    if (it2 != generation_db_->end()) {
      assert(it2->second < UINT32_MAX);
      it2.value()++;
      /* Bubble up the generation change */
      proc->exec_point()->register_file_usage_update(this, FileUsageUpdate(this));
    } else {
      generation_db_->insert({this, 1});
    }
  }
}


/**
 * Return parent dir or nullptr for "/"
 */
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

/**
 * Checks if a path semantically begins with one of the given sorted subpaths.
 *
 * Does string operations only, does not look at the file system.
 */
bool FileName::is_at_locations(const std::vector<std::string> *locations) const {
  for (const std::string& location : *locations) {
    const char *location_name = location.c_str();
    size_t location_len = location.length();
    while (location_len > 0 && location_name[location_len - 1] == '/') {
      location_len--;
    }

    if (this->length_ < location_len) {
      continue;
    }

    if (this->name_[location_len] != '/' && this->length_ > location_len) {
      continue;
    }

    /* Try comparing only the first 8 bytes to potentially save a call to memcmp */
    if (location_len >= sizeof(int64_t)
        && (*reinterpret_cast<const int64_t*>(location_name)
            != *reinterpret_cast<const int64_t*>(this->name_))) {
      /* Does not break the loop if this->name_ > location->name_ */
      // TODO(rbalint) maybe the loop could be broken making this function even faster
      continue;
    }

    const int memcmp_res = memcmp(location_name, this->name_, location_len);
    if (memcmp_res < 0) {
      continue;
    } else if (memcmp_res > 0) {
      return false;
    }

    if (this->length_ == location_len) {
      return true;
    }

    if (this->name_[location_len] == '/') {
      return true;
    }
  }
  return false;
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
