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

class ExecedProcessCacher {
 public:
  static void init(const libconfig::Config* cfg);
  static unsigned int cache_format() {return cache_format_;}
  bool fingerprint(const ExecedProcess *proc);
  void erase_fingerprint(const ExecedProcess *proc);

  void store(ExecedProcess *proc);

  const FBBSTORE_Serialized_process_inputs_outputs *find_shortcut(const ExecedProcess *proc,
                                                                  uint8_t **inouts_buf,
                                                                  size_t *inouts_buf_len);
  bool apply_shortcut(ExecedProcess *proc,
                      const FBBSTORE_Serialized_process_inputs_outputs *inouts,
                      std::vector<int> *fds_appended_to);
  bool shortcut(ExecedProcess *proc, std::vector<int> *fds_appended_to);
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
  ExecedProcessCacher(bool no_store, bool no_fetch, const libconfig::Config* cfg);
  bool env_fingerprintable(const std::string& name_and_value) const;

  bool no_store_;
  bool no_fetch_;
  tsl::hopscotch_set<std::string> envs_skip_;

  /* The hashed fingerprint of the processes handled by this cacher. */
  tsl::hopscotch_map<const ExecedProcess*, Hash> fingerprints_;
  /* The entire fingerprint of the processes handled by this cacher, for debugging
   * purposes, only if debugging is enabled. In serialized FBBFP format. */
  tsl::hopscotch_map<const ExecedProcess*, std::vector<char>> fingerprint_msgs_;

  static unsigned int cache_format_;
  DISALLOW_COPY_AND_ASSIGN(ExecedProcessCacher);
};

/* singleton */
extern ExecedProcessCacher* execed_process_cacher;

}  /* namespace firebuild */
#endif  // FIREBUILD_EXECED_PROCESS_CACHER_H_
