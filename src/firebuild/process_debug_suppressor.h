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
