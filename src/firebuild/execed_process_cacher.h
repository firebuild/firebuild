/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXECED_PROCESS_CACHER_H_
#define FIREBUILD_EXECED_PROCESS_CACHER_H_

#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>

#include <string>
#include <vector>
#include <libconfig.h++>

#include "firebuild/blob_cache.h"
#include "firebuild/obj_cache.h"
#include "firebuild/execed_process.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"

namespace firebuild {

enum stats_type {
  FB_SHOW_STATS_CURRENT,
  FB_SHOW_STATS_STORED,
};

class ExecedProcessCacher {
 public:
  /**
   * One object is responsible for handling the fingerprinting and caching
   * of multiple ExecedProcesses which potentially come from / go to the
   * same cache.
   */
  static void init(const libconfig::Config* cfg);
  static unsigned int cache_format() {return cache_format_;}
  /**
   * Compute the fingerprint, store it keyed by the process in fingerprints_.
   * Also store fingerprint_msgs_ if debugging is enabled.
   */
  bool fingerprint(const ExecedProcess *proc);
  void erase_fingerprint(const ExecedProcess *proc);

  void store(ExecedProcess *proc);

  /**
   * Look up the cache for an entry describing what this process did the
   * last time.
   *
   * This means fetching all the entries corresponding to the process's
   * fingerprint, and finding the one matching the file system.
   *
   * Returns a new object, to be deleted by the caller, if exactly one
   * match was found.
   */
  const FBBSTORE_Serialized_process_inputs_outputs *find_shortcut(const ExecedProcess *proc,
                                                                  uint8_t **inouts_buf,
                                                                  size_t *inouts_buf_len,
                                                                  Subkey* subkey_out);
  bool apply_shortcut(ExecedProcess *proc,
                      const FBBSTORE_Serialized_process_inputs_outputs *inouts,
                      std::vector<int> *fds_appended_to);
  bool shortcut(ExecedProcess *proc, std::vector<int> *fds_appended_to);
  void not_shortcutting() {if (!no_fetch_) not_shortcutting_++;}
  void add_stored_stats();
  void set_self_cpu_time_ms(unsigned int time_ms) {
    self_cpu_time_ms_ = time_ms;
  }
  void print_stats(stats_type what);
  void update_stored_stats();
  void gc();
  /**
   * Checks if the object cache entry can be used for shortcutting, i.e. all the referenced
   * blobs are present in the blob cache and all the referenced system files on the system
   * match the process inputs.
   * @param[in] entry_buf object cache entry as stored
   * @param[out] referenced_blobs if the entry is usable all the referenced blobs are added to this
   *             set
   */
  bool is_entry_usable(uint8_t* entry_buf, tsl::hopscotch_set<AsciiHash>* referenced_blobs);

 private:
  ExecedProcessCacher(bool no_store, bool no_fetch, const std::string& cache_dir,
                      const libconfig::Config* cfg);
  /**
   * Helper for fingerprint() to decide which env vars matter
   */
  bool env_fingerprintable(const std::string& name_and_value) const;

  bool no_store_;
  bool no_fetch_;
  tsl::hopscotch_set<std::string> envs_skip_;
  unsigned int shortcut_attempts_ {0};
  unsigned int shortcut_hits_ {0};
  unsigned int not_shortcutting_ {0};
  int64_t self_cpu_time_ms_ {0};
  int64_t cache_saved_cpu_time_ms_ {0};

  /** The hashed fingerprint of configured ignore locations. */
  Hash ignore_locations_hash_;
  /* The hashed fingerprint of the processes handled by this cacher. */
  tsl::hopscotch_map<const ExecedProcess*, Hash> fingerprints_;
  /* The entire fingerprint of the processes handled by this cacher, for debugging
   * purposes, only if debugging is enabled. In serialized FBBFP format. */
  tsl::hopscotch_map<const ExecedProcess*, std::vector<char>> fingerprint_msgs_;

  static unsigned int cache_format_;
  std::string cache_dir_;
  DISALLOW_COPY_AND_ASSIGN(ExecedProcessCacher);
};

/* singleton */
extern ExecedProcessCacher* execed_process_cacher;

}  /* namespace firebuild */
#endif  // FIREBUILD_EXECED_PROCESS_CACHER_H_
