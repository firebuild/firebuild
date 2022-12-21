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

#ifndef FIREBUILD_PROCESS_FACTORY_H_
#define FIREBUILD_PROCESS_FACTORY_H_

#include <memory>
#include <vector>

#include "./fbbcomm.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/process_tree.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

/**
 * Converts FBB messages from monitored processes to new Process
 * instances. It is an implementation of the GoF Factory pattern.
 * The class itself is never instantiated, but groups a set of
 * static functions which accept a ProcessTree reference and an incoming FBB
 * message to the process from.
 */
class ProcessFactory {
 public:
  static ForkedProcess* getForkedProcess(int pid, Process * const parent);
  static ExecedProcess* getExecedProcess(const FBBCOMM_Serialized_scproc_query *const msg,
                                         Process * const parent,
                                         std::vector<std::shared_ptr<FileFD>>* fds);
  static bool peekProcessDebuggingSuppressed(const FBBCOMM_Serialized *fbbcomm_buf);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessFactory);
};

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_FACTORY_H_
