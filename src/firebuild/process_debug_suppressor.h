/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_DEBUG_SUPPRESSOR_H_
#define FIREBUILD_PROCESS_DEBUG_SUPPRESSOR_H_


#include "firebuild/debug.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ProcessDebugSuppressor {
 public:
  explicit ProcessDebugSuppressor(const Process* const proc)
      : debug_suppressed_changed_(proc), debug_suppressed_orig_(debug_suppressed) {
    if (proc && firebuild::debug_filter) {
      debug_suppressed = proc->debug_suppressed();
    }
  }

  ~ProcessDebugSuppressor() {
    if (debug_suppressed_changed_) {
      debug_suppressed = debug_suppressed_orig_;
    }
  }
 private:
  bool debug_suppressed_changed_;
  bool debug_suppressed_orig_;
};

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_DEBUG_SUPPRESSOR_H_
