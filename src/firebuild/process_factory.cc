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

#include "firebuild/process_factory.h"

#include <string>
#include <utility>

#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"

namespace firebuild {

ForkedProcess*
ProcessFactory::getForkedProcess(const int pid, Process * const parent) {
  TRACK(FB_DEBUG_PROC, "pid=%d, parent=%s", pid, D(parent));

  return new ForkedProcess(pid, parent->pid(), parent, parent->pass_on_fds(false));
}

ExecedProcess*
ProcessFactory::getExecedProcess(const FBBCOMM_Serialized_scproc_query *const msg,
                                 Process * const parent,
                                 std::vector<std::shared_ptr<FileFD>>* fds) {
  TRACK(FB_DEBUG_PROC, "parent=%s", D(parent));

  const FileName* executable = FileName::Get(msg->get_executable());
  const FileName* executed_path = msg->has_executed_path()
      ? FileName::Get(msg->get_executed_path()) : executable;
  char* original_executed_path = msg->has_original_executed_path() ?
      strndup(msg->get_original_executed_path(), msg->get_original_executed_path_len())
      : const_cast<char*>(executed_path->c_str());
  const size_t libs_count = msg->get_libs_count();
  std::vector<const FileName*> libs(libs_count);
  for (size_t i = 0; i < libs_count; i++) {
    libs[i] = (FileName::Get(msg->get_libs_at(i), msg->get_libs_len_at(i)));
  }
  auto e = new ExecedProcess(msg->get_pid(),
                             msg->get_ppid(),
                             FileName::Get(msg->get_cwd()),
                             executable, executed_path, original_executed_path,
                             msg->get_arg_as_vector(),
                             msg->get_env_var_as_vector(),
                             std::move(libs),
                             msg->get_umask(),
                             parent,
                              /* When processing this msg the suppression is already set globally,
                               * or for this thread. */
                             debug_suppressed,
                             fds);

  /* Debug the full command line, env vars etc. */
  FB_DEBUG(FB_DEBUG_PROC, "Created ExecedProcess " + d(e, 1) + " with:");
  FB_DEBUG(FB_DEBUG_PROC, "- exe = " + d(e->executable()));
  FB_DEBUG(FB_DEBUG_PROC, "- arg = " + d(e->args()));
  FB_DEBUG(FB_DEBUG_PROC, "- cwd = " + d(e->initial_wd()));
  FB_DEBUG(FB_DEBUG_PROC, "- env = " + d(e->env_vars()));
  FB_DEBUG(FB_DEBUG_PROC, "- lib = " + d(msg->get_libs_as_vector()));
  FB_DEBUG(FB_DEBUG_PROC, "- umask = " + d(e->umask()));

  if (msg->has_jobserver_fifo()) {
    e->set_jobserver_fifo(msg->get_jobserver_fifo());
    FB_DEBUG(FB_DEBUG_PROC, "- jobserver_fifo = " + d(e->jobserver_fifo()));
  } else {
    if (fbbcomm_serialized_scproc_query_get_jobserver_fds_count(msg) == 2) {
      const int* jobserver_fds = fbbcomm_serialized_scproc_query_get_jobserver_fds(msg);
      e->maybe_set_jobserver_fds(jobserver_fds[0], jobserver_fds[1]);
      FB_DEBUG(FB_DEBUG_PROC, "- jobserver_fd_r = " + d(e->jobserver_fd_r()));
      FB_DEBUG(FB_DEBUG_PROC, "- jobserver_fd_w = " + d(e->jobserver_fd_w()));
    }
  }
  return e;
}

bool ProcessFactory::peekProcessDebuggingSuppressed(const FBBCOMM_Serialized *fbbcomm_buf) {
  if (!debug_filter) {
    return false;
  }

  const int tag = fbbcomm_buf->get_tag();
  if (tag == FBBCOMM_TAG_scproc_query) {
    const FBBCOMM_Serialized_scproc_query *msg =
        reinterpret_cast<const FBBCOMM_Serialized_scproc_query *>(fbbcomm_buf);
    const FileName* executable = FileName::Get(msg->get_executable());
    const FileName* executed_path = msg->has_executed_path()
        ? FileName::Get(msg->get_executed_path()) : executable;
    const auto args = msg->get_arg_as_vector();
    return !debug_filter->match(executable, executed_path, args.size() > 0 ? args[0] : "");
  } else if (tag == FBBCOMM_TAG_fork_child) {
    const FBBCOMM_Serialized_fork_child *msg =
        reinterpret_cast<const FBBCOMM_Serialized_fork_child *>(fbbcomm_buf);
    const auto ppid = msg->get_ppid();
    auto pproc = proc_tree->pid2proc(ppid);
    assert(pproc);
    return !debug_filter->match(pproc->exec_point());
  } else {
    assert(0 && "unexpected tag");
    return false;
  }
}

}  /* namespace firebuild */
