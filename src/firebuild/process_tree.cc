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

#include "firebuild/process_tree.h"

#include <math.h>
#include <cstdio>

#include "common/platform.h"
#include "firebuild/debug.h"

extern bool generate_report;

namespace firebuild {

/* singleton */
ProcessTree *proc_tree;

ProcessTree::~ProcessTree() {
  TRACK(FB_DEBUG_PROCTREE, "");

  /* clean up all processes, from the leaves towards the root */
  delete_process_subtree(root());
  /* clean up pending exec() children */
  for (auto& pair : pid2exec_child_sock_) {
    delete(pair.second.incomplete_child);
  }
  /* clean up pending posix_spawn() children */
  for (auto& pair : pid2posix_spawn_child_sock_) {
    delete(pair.second.incomplete_child);
  }
}

void ProcessTree::delete_process_subtree(Process *p) {
  if (!p) {
    return;
  }
  delete_process_subtree(p->exec_child());
  for (ForkedProcess *fork_child : p->fork_children()) {
    delete_process_subtree(fork_child);
  }
  delete p;
}

void ProcessTree::insert_process(Process *p) {
  TRACK(FB_DEBUG_PROCTREE, "p=%s", D(p));

  fb_pid2proc_[p->fb_pid()] = p;
  pid2proc_[p->pid()] = p;
}

void ProcessTree::insert(Process *p) {
  TRACK(FB_DEBUG_PROCTREE, "p=%s", D(p));

  assert(p->fork_point());
  assert(p->exec_point() || p == root_);
  if (p->parent() == NULL && p != root_) {
    /* root's parent is firebuild which is not in the tree.
     * If any other parent is missing, Firebuild missed process
     * that can happen due to the missing process(es) being statically built */
    fb_error("TODO(rbalint) handle: Process without known exec parent\n");
  }
  insert_process(p);
}

void ProcessTree::insert_root(pid_t root_pid, int stdin_fd, int stdout_fd, int stderr_fd) {
  TRACK(FB_DEBUG_PROCTREE, "root_pid=%d", root_pid);
  root_ = new firebuild::ForkedProcess(root_pid, getpid(), nullptr,
                                       new std::vector<std::shared_ptr<FileFD>>());
  root_->set_state(firebuild::FB_PROC_TERMINATED);
  // TODO(rbalint) support other inherited fds
  /* Create the FileFD representing stdin of the top process. Pipe will be NULL, that's fine. */
  root_->add_filefd(stdin_fd, std::make_shared<FileFD>(stdin_fd, O_RDONLY, nullptr, nullptr));

  /* Create the Pipes and FileFDs representing stdout and stderr of the top process. */
  // FIXME Make this more generic, for all the received pipes / terminal outputs.
  bool stdout_stderr_match = fdcmp(stdout_fd, stderr_fd) == 0;
  FB_DEBUG(FB_DEBUG_PROCTREE, stdout_stderr_match ? "Top level stdout and stderr are the same" :
           "Top level stdout and stderr are distinct");

  std::shared_ptr<Pipe> pipe;
  for (auto fd : {stdout_fd, stderr_fd}) {
    if (fd == stderr_fd && stdout_stderr_match) {
      /* stdout and stderr point to the same location (changing one's flags did change the
       * other's). Reuse the Pipe object that we created in the loop's first iteration. */
      root_->add_filefd(fd, std::make_shared<FileFD>(fd, (*root_->fds())[stdout_fd], false));
    } else {
      /* Create a new Pipe for this file descriptor.
       * The fd keeps blocking/non-blocking behaviour, it seems to be ok with epoll.
       * The fd is dup()-ed first to let it be closed without closing the original fd. */
      int fd_dup = fcntl(fd, F_DUPFD_CLOEXEC, stderr_fd + 1);
      assert(fd_dup != -1);
#ifdef __clang_analyzer__
      /* Scan-build reports a false leak for the correct code. This is used only in static
       * analysis. It is broken because all shared pointers to the Pipe must be copies of
       * the shared self pointer stored in it. */
      pipe = std::make_shared<Pipe>(fd_dup, nullptr);
#else
      pipe = (new Pipe(fd_dup, nullptr))->shared_ptr();
#endif
      FB_DEBUG(FB_DEBUG_PIPE, "created pipe with fd0: " + d(fd) + ", dup()-ed as: " + d(fd_dup));
      /* Top level inherited fds are special, they should not be closed. */
      inherited_fd_pipes_.insert(pipe);
      root_->add_filefd(fd, std::make_shared<FileFD>(fd, O_WRONLY, pipe, nullptr));
    }
  }
  insert_process(root_);
}

void ProcessTree::GcProcesses() {
  while (!proc_gc_queue_.empty()) {
    ExecedProcess* proc = proc_gc_queue_.front();
    proc->inherited_files().clear();
    if (!generate_report) {
      proc->file_usages().clear();
      proc->args().clear();
      proc->env_vars().clear();
      proc->libs().clear();
    }
    proc_gc_queue_.pop();
  }
}
}  /* namespace firebuild */
