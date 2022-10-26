/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

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
  /** Add stored hit statistics and cache size to current run's counters. */
  void add_stored_stats();
  void set_self_cpu_time_ms(unsigned int time_ms) {
    self_cpu_time_ms_ = time_ms;
  }
  void print_stats(stats_type what);
  void update_stored_stats();
  /** Get bytes stored in the cache reading cachedir/size file. */
  ssize_t get_stored_bytes_from_cache() const;
  void read_stored_cached_bytes();
  /** Store number of bytes cached to cachedir/size file. */
  void update_stored_bytes();
  /** Register cache size change occurred in the current run. */
  void update_cached_bytes(ssize_t bytes) {this_runs_cached_bytes_ += bytes;}
  /* A garbage collection run is needed, e.g. because the cache is too big. */
  bool is_gc_needed() const;
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
  /**
   * Number of bytes added to (or freed from) the cache in the current run.
   * It can be negative in case of a garbage collection run.
   * "This run" means the lifetime for the firebuild process including potentially
   * running a build command and running gc(), including potentially processing the cache
   * multiple times in ExecedProcessCacher::gc().
   */
  ssize_t this_runs_cached_bytes_ {0};
  /** Number of bytes in the cache as stored in the cachedir/size file. */
  ssize_t stored_cached_bytes_ {0};
  unsigned int gc_runs_ {0};

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
