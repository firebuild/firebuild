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

/**
 * Context of an interceptor connection.
 */

#ifndef FIREBUILD_CONNECTION_CONTEXT_H_
#define FIREBUILD_CONNECTION_CONTEXT_H_

#include <unistd.h>

#include <string>

#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"
#include "firebuild/epoll.h"
#include "firebuild/execed_process.h"
#include "firebuild/message_processor.h"
#include "firebuild/linear_buffer.h"
#include "firebuild/process.h"
#include "firebuild/process_tree.h"

extern firebuild::Epoll *epoll;

namespace firebuild {

class ConnectionContext {
 public:
  explicit ConnectionContext(int conn)
      : buffer_(), conn_(conn) {}
  ~ConnectionContext() {
    if (proc) {
      auto exec_child_sock = proc_tree->Pid2ExecChildSock(proc->pid());
      if (exec_child_sock) {
        auto exec_child = exec_child_sock->incomplete_child;
        exec_child->set_fds(proc->pass_on_fds());
        MessageProcessor::accept_exec_child(exec_child, exec_child_sock->sock);
        proc_tree->DropQueuedExecChild(proc->pid());
      }
      proc->finish();
    }
    assert(conn_ >= 0);
    epoll->maybe_del_fd(conn_, EPOLLIN);
    close(conn_);
    conn_ = -1;
  }
  LinearBuffer& buffer() {return buffer_;}
  Process * proc = nullptr;

 private:
  /** Partial interceptor message including the FBB header */
  LinearBuffer buffer_;
  int conn_;
  DISALLOW_COPY_AND_ASSIGN(ConnectionContext);
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
inline std::string d(const ConnectionContext& ctx, const int level = 0) {
  (void)level;  /* unused */
  return "{ConnectionContext proc=" + d(ctx.proc) + "}";
}
inline std::string d(const ConnectionContext *ctx, const int level = 0) {
  if (ctx) {
    return d(*ctx, level);
  } else {
    return "{ConnectionContext NULL}";
  }
}

}  /* namespace firebuild */

#endif  // FIREBUILD_CONNECTION_CONTEXT_H_
