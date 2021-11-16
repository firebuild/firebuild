/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXECED_PROCESS_CACHER_H_
#define FIREBUILD_EXECED_PROCESS_CACHER_H_

#include <tsl/hopscotch_map.h>

#include <string>
#include <unordered_set>
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
  ExecedProcessCacher(bool no_store,
                      bool no_fetch,
                      const libconfig::Setting& envs_skip);

  bool fingerprint(const ExecedProcess *proc);
  void erase_fingerprint(const ExecedProcess *proc);

  void store(const ExecedProcess *proc);

  const FBBSTORE_Serialized_process_inputs_outputs *find_shortcut(const ExecedProcess *proc,
                                                                  uint8_t **inouts_buf,
                                                                  size_t *inouts_buf_len);
  bool apply_shortcut(ExecedProcess *proc,
                      const FBBSTORE_Serialized_process_inputs_outputs *inouts);
  bool shortcut(ExecedProcess *proc);

 private:
  bool env_fingerprintable(const std::string& name_and_value) const;

  bool no_store_;
  bool no_fetch_;
  std::unordered_set<std::string> envs_skip_;

  /* The hashed fingerprint of the processes handled by this cacher. */
  tsl::hopscotch_map<const ExecedProcess*, Hash> fingerprints_;
  /* The entire fingerprint of the processes handled by this cacher, for debugging
   * purposes, only if debugging is enabled. In serialized FBBFP format. */
  tsl::hopscotch_map<const ExecedProcess*, std::vector<char>> fingerprint_msgs_;

  DISALLOW_COPY_AND_ASSIGN(ExecedProcessCacher);
};

}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_CACHER_H_
