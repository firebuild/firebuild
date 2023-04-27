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

#include "firebuild/sigchild_callback.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <string>

#include "firebuild/debug.h"
#include "firebuild/firebuild.h"
#include "firebuild/process_debug_suppressor.h"
#include "firebuild/process_tree.h"

namespace firebuild {

static void save_child_status(pid_t pid, int status, int * ret, bool orphan) {
  TRACK(FB_DEBUG_PROC, "pid=%d, status=%d, orphan=%s", pid, status, D(orphan));

  if (WIFEXITED(status)) {
    *ret = WEXITSTATUS(status);
    Process* proc = proc_tree->pid2proc(pid);
    if (proc && proc->fork_point()) {
      proc->fork_point()->set_exit_status(*ret);
    }
    FB_DEBUG(FB_DEBUG_COMM, std::string(orphan ? "orphan" : "child")
             + " process exited with status " + std::to_string(*ret) + ". ("
             + d(proc_tree->pid2proc(pid)) + ")");
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr, "%s process has been killed by signal %d\n",
            orphan ? "Orphan" : "Child",
            WTERMSIG(status));
  }
}

/* This is the actual business logic for SIGCHLD, called synchronously when processing the events
 * returned by epoll_wait(). */
void sigchild_cb(const struct epoll_event* event, void *arg) {
  TRACK(FB_DEBUG_PROC, "");

  (void)event;  /* unused */
  (void)arg;    /* unused */

  char dummy;
  int read_ret = read(sigchild_selfpipe[0], &dummy, 1);
  (void)read_ret;  /* unused */

  int status = 0;
  pid_t waitpid_ret;

  /* Collect exiting children. */
  do {
    waitpid_ret = waitpid(-1, &status, WNOHANG);
    if (waitpid_ret == child_pid) {
      /* This is the top process the supervisor started. */
      Process* proc = proc_tree->pid2proc(child_pid);
      assert(proc);
      ProcessDebugSuppressor debug_suppressor(proc);
      save_child_status(waitpid_ret, status, &child_ret, false);
      proc->set_been_waited_for();
    } else if (waitpid_ret > 0) {
      /* This is an orphan process. Its fork parent quit without wait()-ing for it
       * and as a subreaper the supervisor received the SIGCHLD for it. */
      Process* proc = proc_tree->pid2proc(waitpid_ret);
      if (proc) {
        /* Since the parent of this orphan process did not wait() for it, it will not be stored in
         * the cache even when finalizing it. */
        assert(!proc->been_waited_for());
      }
      int ret = -1;
      save_child_status(waitpid_ret, status, &ret, true);
    }
  } while (waitpid_ret > 0);

  if (waitpid_ret < 0) {
    /* All children exited. Stop listening on the socket, and set listener to -1 to tell the main
     * epoll loop to quit. */
    if (listener > 0) {
      epoll->del_fd(listener);
      close(listener);
      listener = -1;
    }
  }
}


}  /* namespace firebuild */
