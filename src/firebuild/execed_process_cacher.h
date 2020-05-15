/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EXECED_PROCESS_CACHER_H_
#define FIREBUILD_EXECED_PROCESS_CACHER_H_

#include <string>
#include <unordered_map>
#include <libconfig.h++>

#include "firebuild/cache.h"
#include "firebuild/multi_cache.h"
#include "firebuild/execed_process.h"
#include "firebuild/hash.h"
#include "firebuild/fb-cache.pb.h"

namespace firebuild {

class ExecedProcessCacher {
 public:
  ExecedProcessCacher(Cache *cache,
                      MultiCache *multi_cache,
                      bool no_store,
                      bool no_fetch,
                      const libconfig::Setting& envs_skip);

  bool fingerprint(const ExecedProcess *proc);
  void erase_fingerprint(const ExecedProcess *proc);

  void store(const ExecedProcess *proc);

  msg::ProcessInputsOutputs *find_shortcut(const ExecedProcess *proc);
  bool apply_shortcut(ExecedProcess *proc,
                      const msg::ProcessInputsOutputs& outputs);
  bool shortcut(ExecedProcess *proc);

 private:
  bool env_fingerprintable(const std::string& name_and_value) const;

  Cache *cache_;
  MultiCache *multi_cache_;
  bool no_store_;
  bool no_fetch_;
  const libconfig::Setting& envs_skip_;

  /* The hashed fingerprint of the processes handled by this cacher. */
  std::unordered_map<const ExecedProcess*, Hash> fingerprints_;
  /* The entire fingerprint of the processes handled by this cacher, for debugging
   * purposes, only if debugging is enabled. */
  std::unordered_map<const ExecedProcess*, msg::ProcessFingerprint*> fingerprint_msgs_;

  DISALLOW_COPY_AND_ASSIGN(ExecedProcessCacher);
};

}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_CACHER_H_
