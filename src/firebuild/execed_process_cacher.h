/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECEDPROCESSCACHER_H_
#define FIREBUILD_EXECEDPROCESSCACHER_H_

#include "firebuild/cache.h"
#include "firebuild/multi_cache.h"
#include "firebuild/execed_process.h"

namespace firebuild {

class ExecedProcessCacher {
 public:
  ExecedProcessCacher(Cache *cache,
                      MultiCache *multi_cache,
                      bool no_store);

  void store(const ExecedProcess *proc);

 private:
  Cache *cache_;
  MultiCache *multi_cache_;
  bool no_store_;
};

}  // namespace firebuild
#endif  // FIREBUILD_EXECEDPROCESSCACHER_H_
