/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Context of an interceptor connection.
 */

#ifndef FIREBUILD_CONNECTION_CONTEXT_H_
#define FIREBUILD_CONNECTION_CONTEXT_H_

#include <event2/event.h>
#include <unistd.h>

#include "firebuild/cxx_lang_utils.h"
#include "firebuild/execed_process.h"
#include "firebuild/linear_buffer.h"
#include "firebuild/process.h"
#include "firebuild/process_tree.h"

namespace firebuild {

extern void accept_exec_child(ExecedProcess* proc, int fd_conn,
                              ProcessTree* proc_tree);

class ConnectionContext {
 public:
  explicit ConnectionContext(ProcessTree *proc_tree) : buffer_(), proc_tree_(proc_tree) {}
  ~ConnectionContext() {
    if (proc) {
      auto exec_child_sock = proc_tree_->Pid2ExecChildSock(proc->pid());
      if (exec_child_sock) {
        auto exec_child = exec_child_sock->incomplete_child;
        exec_child->set_fds(proc->pass_on_fds());
        accept_exec_child(exec_child, exec_child_sock->sock, proc_tree_);
        proc_tree_->DropQueuedExecChild(proc->pid());
      }
      proc->finish();
    }
    assert(ev_);
    evutil_socket_t conn = event_get_fd(ev_);
    event_free(ev_);
    close(conn);
  }
  void set_ev(struct event* ev) {ev_ = ev;}
  LinearBuffer& buffer() {return buffer_;}
  Process * proc = nullptr;

 private:
  /** Partial interceptor message including the FBB header */
  LinearBuffer buffer_;
  ProcessTree *proc_tree_;
  struct event* ev_ = nullptr;
  DISALLOW_COPY_AND_ASSIGN(ConnectionContext);
};

}  // namespace firebuild

#endif  // FIREBUILD_CONNECTION_CONTEXT_H_
