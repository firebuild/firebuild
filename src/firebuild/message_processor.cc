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

#include "firebuild/message_processor.h"

#include <sys/random.h>
#if defined (__APPLE__)
#include <sys/spawn.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/firebuild_common.h"
#include "firebuild/config.h"
#include "firebuild/debug.h"
#include "firebuild/connection_context.h"
#include "firebuild/epoll.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"
#include "firebuild/execed_process.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/pipe.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/process.h"
#include "firebuild/process_factory.h"
#include "firebuild/process_debug_suppressor.h"
#include "firebuild/process_tree.h"
#include "firebuild/process_fbb_adaptor.h"
#include "firebuild/utils.h"
#include "./fbbcomm.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"

namespace firebuild {

static void reject_exec_child(int fd_conn) {
    FBBCOMM_Builder_scproc_resp sv_msg;
    sv_msg.set_dont_intercept(true);
    sv_msg.set_shortcut(false);

    send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&sv_msg));
}

void MessageProcessor::accept_exec_child(ExecedProcess* proc, int fd_conn,
                                         int fd0_reopen) {
    TRACKX(FB_DEBUG_PROC, 1, 1, Process, proc, "fd_conn=%s, fd0_reopen=%s",
        D_FD(fd_conn), D_FD(fd0_reopen));

    /* We build up an FBB referring to this value, so it has to be valid until we send that FBB. */
    const int stdin_fileno = STDIN_FILENO;

    FBBCOMM_Builder_scproc_resp sv_msg;

    /* These two have the same number of items and they correspond to each other.
     * "reopened_dups" is for the "reopen_fd_fifos" array in FBB "scproc_resp",
     * "fifo_fds" is for the ancillary data. */
    std::vector<const FBBCOMM_Builder *> reopened_dups = {};
    std::vector<int> fifo_fds = {};

    proc_tree->insert(proc);
    proc->initialize();

    if (shortcut_allow_list_matcher && !shortcut_allow_list_matcher->match(proc)) {
      proc->disable_shortcutting_only_this("Executable is not allowed to be shortcut");
      execed_process_cacher->not_shortcutting();
    }

    if (dont_intercept_matcher->match(proc)) {
      /* Executables that should not be intercepted. */
      proc->disable_shortcutting_bubble_up("Executable set to not be intercepted");
      execed_process_cacher->not_shortcutting();
      sv_msg.set_dont_intercept(true);
    } else if (dont_shortcut_matcher->match(proc)) {
      if (quirks & FB_QUIRK_LTO_WRAPPER && proc->args().size() > 0 && proc->args()[0] == "make"
          && proc->parent_exec_point()
          && proc->parent_exec_point()->executable()->without_dirs() == "lto-wrapper" ) {
        FB_DEBUG(FB_DEBUG_PROC, "Allow shortcutting lto-wrapper's make (lto-wrapper quirk)");
      } else {
        /* Executables that are known not to be shortcuttable. */
        proc->disable_shortcutting_bubble_up("Executable set to be not shortcut");
        execed_process_cacher->not_shortcutting();
      }
    }

    /* Check for executables that we prefer not to shortcut. */
    if (skip_cache_matcher->match(proc)) {
      proc->disable_shortcutting_only_this("Executable matches skip_cache");
      execed_process_cacher->not_shortcutting();
    }

    /* If we still potentially can, and prefer to cache / shortcut this process,
     * register the cacher object and calculate the process's fingerprint. */
    if (proc->can_shortcut()) {
      if (!execed_process_cacher->fingerprint(proc)) {
        proc->disable_shortcutting_bubble_up("Could not fingerprint the process");
      }
    }

    std::vector<inherited_file_t> inherited_files = proc->inherited_files();
    for (inherited_file_t& inherited_file : inherited_files) {
      if (inherited_file.type == FD_PIPE_OUT) {
        /* There may be incoming data from the (transitive) parent(s), drain it.
         * Do it before trying to shortcut. */
        auto pipe = proc->get_fd(inherited_file.fds[0])->pipe();
        assert(pipe);
        pipe->drain();
      }
    }

    /* Try to shortcut the process. */
    std::vector<int> fds_appended_to, seekable_fds;
    std::vector<int64_t> seekable_fds_size;
    bool shortcutting_succeeded = proc->shortcut(&fds_appended_to);
    if (shortcutting_succeeded) {
      sv_msg.set_shortcut(true);
      sv_msg.set_exit_status(proc->fork_point()->exit_status());
      sv_msg.set_fds_appended_to(fds_appended_to);
      if (fd0_reopen >= 0) {
        close(fd0_reopen);
      }
    } else {
      sv_msg.set_shortcut(false);
      /* parent forked, thus a new set of fds is needed to track outputs */

      /* For popen(..., "w") pipes we couldn't reopen its stdin in the short-lived forked process,
       * so connect our Pipe object with the stdin of the child process here.
       * (The stdout side of a popen(..., "r") child is handled below by the generic
       * code that reopens all inherited outgoing pipes.) */
      if (fd0_reopen >= 0) {
        fifo_fds.push_back(fd0_reopen);
        /* alloca()'s lifetime is the entire function, not just the brace-block. This is what we
         * need because the data has to live until the send_fbb() below. */
        auto dups = reinterpret_cast<FBBCOMM_Builder_scproc_resp_reopen_fd *>(
            alloca(sizeof(FBBCOMM_Builder_scproc_resp_reopen_fd)));
        dups->init();
        dups->set_fds(&stdin_fileno, 1);
        reopened_dups.push_back(reinterpret_cast<FBBCOMM_Builder *>(dups));
      }

      // TODO(rbalint) skip reopening fd if parent's other forked processes closed the fd
      // without writing to it
      const int jobserver_fd_w = proc->jobserver_fd_w();
      for (inherited_file_t& inherited_file : inherited_files) {
        if (inherited_file.type == FD_PIPE_OUT) {
          if (inherited_file.fds[0] == jobserver_fd_w
              && inherited_file.fds.size() == 1) {
            /* Skip reopening the jobserver pipe. */
            continue;
          }
          auto file_fd_old = proc->get_shared_fd(inherited_file.fds[0]);
          auto pipe = file_fd_old->pipe();
          assert(pipe);

          /* As per #689, reopening the pipes causes different behavior than without firebuild. With
           * firebuild, across an exec they no longer share the same "open file description" and thus
           * the fcntl flags. Perform this unduping from the exec parent, i.e. modify the FileFDs to
           * point to a new FileOFD. */
          auto fds = proc->fds();
          int fd = inherited_file.fds[0];
          auto file_fd = std::make_shared<FileFD>(file_fd_old->flags(), pipe,
                                                  file_fd_old->opened_by());
          (*fds)[fd] = file_fd;
          for (size_t i = 1; i < inherited_file.fds.size(); i++) {
            fd = inherited_file.fds[i];
            auto file_fd_dup = std::make_shared<FileFD>(file_fd, false);
            (*fds)[fd] = file_fd_dup;
          }

          /* Create a new unnamed pipe. */
          int fifo_fd[2];
          int ret = fb_pipe2(fifo_fd, file_fd->flags() & ~O_ACCMODE);
          (void)ret;
          assert(ret == 0);
          if (epoll->is_added_fd(fifo_fd[0])) {
            fifo_fd[0] = epoll->remap_to_not_added_fd(fifo_fd[0]);
          }
          bump_fd_age(fifo_fd[0]);
          /* The supervisor needs nonblocking fds for the pipes. */
          fcntl(fifo_fd[0], F_SETFL, O_NONBLOCK);

          /* Find the recorders belonging to the parent process. We need to record to all those,
           * plus create a new recorder for ourselves (unless shortcutting is already disabled). */
          auto  recorders =  proc->parent() ? pipe->proc2recorders[proc->parent_exec_point()]
              : std::vector<std::shared_ptr<PipeRecorder>>();
          if (proc->can_shortcut()) {
            inherited_file.recorder = std::make_shared<PipeRecorder>(proc);
            recorders.push_back(inherited_file.recorder);
          }
          pipe->add_fd1_and_proc(fifo_fd[0], file_fd.get(), proc, std::move(recorders));
          FB_DEBUG(FB_DEBUG_PIPE, "reopening process' fd: "+ d(inherited_file.fds[0])
                   + " as new fd1: " + d(fifo_fd[0]) + " of " + d(pipe));

          fifo_fds.push_back(fifo_fd[1]);
          /* alloca()'s lifetime is the entire function, not just the brace-block. This is what we
           * need because the data has to live until the send_fbb() below.
           * Calling alloca() from a loop is often frowned upon because it can quickly eat up the
           * stack. Here we only need a tiny amount of data, typically less than 10 integers in all
           * the alloca()d areas combined. */
          auto dups = reinterpret_cast<FBBCOMM_Builder_scproc_resp_reopen_fd *>(
              alloca(sizeof(FBBCOMM_Builder_scproc_resp_reopen_fd)));
          dups->init();
          dups->set_fds(inherited_file.fds);
          reopened_dups.push_back(reinterpret_cast<FBBCOMM_Builder *>(dups));
        } else if (inherited_file.type == FD_FILE) {
          int fd = inherited_file.fds[0];
          auto file_fd = proc->get_shared_fd(fd);
          /* The current offset won't matter for writes. */
          if (!(file_fd->flags() & O_APPEND)) {
            seekable_fds.push_back(fd);
            seekable_fds_size.push_back(inherited_file.start_offset);
          }
        }
        sv_msg.set_seekable_fds(seekable_fds);
        sv_msg.set_seekable_fds_size(seekable_fds_size);
      }

      sv_msg.set_reopen_fds(reopened_dups);

      /* inherited_files was updated with the recorders, save the new version */
      proc->set_inherited_files(inherited_files);

      if (debug_flags != 0) {
        sv_msg.set_debug_flags(debug_flags);
      }
    }

    /* Send "scproc_resp", possibly with attached fds to reopen. */
    send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&sv_msg),
             fifo_fds.data(), fifo_fds.size());

    /* Close the sides that we transferred to the interceptor. This includes the stdin of a
     * popen(... "w") child, as well as the inherited outgoing pipes of every process. */
    for (int fd : fifo_fds) {
      close(fd);
    }
}

/* This is run when we've received both the parent's "popen_parent" and the child's "scproc_query"
 * message, no matter in what order they arrived. */
static void accept_popen_child(Process* unix_parent, const pending_popen_t *pending_popen) {
  ExecedProcess *proc = pending_popen->child;

  /* This is for the special treatment of the fd if the process does another popen(). */
  unix_parent->AddPopenedProcess(pending_popen->fd, proc);

  /* The short-lived forked process was added in proc_new_process_msg() when "scproc_query" arrived.
   *
   * Now we create the Pipe object and register its file handles for the execed process.
   *
   * TODO We should ideally register it to new process's exec parent (the short-lived fork of the
   * popening process) too. However, it really doesn't matter. */

  int up[2], down[2];
  int fd_send_to_parent;
  int fd0_reopen = -1;
  int flags = pending_popen->type_flags;
  if (is_rdonly(flags)) {
    /* For popen(..., "r") (parent reads <- child writes) create only the parent-side backing Unix
     * pipe, and the Pipe object. The child-side backing Unix pipe will be created in
     * accept_exec_child() when reopening the inherited outgoing pipes. */
    FB_DEBUG(FB_DEBUG_PROC, "This is a popen(..., \"r...\") child");

    if (fb_pipe2(down, flags & ~O_ACCMODE) < 0) {
      assert(0 && "pipe2() failed");
    }
    bump_fd_age(down[0]);
    bump_fd_age(down[1]);
    FB_DEBUG(FB_DEBUG_PROC, "down[0]: " + d_fd(down[0]) + ", down[1]: " + d_fd(down[1]));

    fd_send_to_parent = down[0];

    if (!(flags & O_NONBLOCK)) {
      /* The supervisor needs nonblocking fds for the pipes. */
      fcntl(down[1], F_SETFL, flags | O_NONBLOCK);
    }

#ifdef __clang_analyzer__
    /* Scan-build reports a false leak for the correct code. This is used only in static
     * analysis. It is broken because all shared pointers to the Pipe must be copies of
     * the shared self pointer stored in it. */
    auto pipe = std::make_shared<Pipe>(down[1] /* server fd */, unix_parent);
#else
    auto pipe = (new Pipe(down[1] /* server fd */, unix_parent))->shared_ptr();
#endif

    /* The reading side of this pipe is in the popening (parent) process. */
    auto ffd0 = std::make_shared<FileFD>((flags & ~O_ACCMODE) | O_RDONLY,
                                         pipe->fd0_shared_ptr(),
                                         unix_parent /* creator */,
                                         true /* close_on_popen */);
    unix_parent->add_filefd(pending_popen->fd /* client fd */, ffd0);

    /* The writing side of this pipe is in the forked and the execed processes.
     * We're lazy and we don't register it for the forked process, no one cares. */
    auto ffd1 = std::make_shared<FileFD>((flags & ~O_ACCMODE) | O_WRONLY,
                                         pipe->fd1_shared_ptr(),
                                         unix_parent /* creator */,
                                         false /* close_on_popen */);
    proc->add_filefd(STDOUT_FILENO /* client fd */, ffd1);
  } else {
    /* For popen(..., "w") (parent writes -> child reads) create both backing Unix unnamed
     * pipes, as well as the Pipe object handling them. */
    FB_DEBUG(FB_DEBUG_PROC, "This is a popen(..., \"w...\") child");

    if (fb_pipe2(up, flags & ~O_ACCMODE) < 0 || fb_pipe2(down, flags & ~O_ACCMODE) < 0) {
      assert(0 && "pipe2() failed");
    }
    if (epoll->is_added_fd(up[0])) {
      up[0] = epoll->remap_to_not_added_fd(up[0]);
    }

    bump_fd_age(up[0]);
    bump_fd_age(up[1]);
    bump_fd_age(down[0]);
    bump_fd_age(down[1]);
    FB_DEBUG(FB_DEBUG_PROC, "up[0]: " + d_fd(up[0]) + ", up[1]: " + d_fd(up[1]) +
                            ", down[0]: " + d_fd(down[0]) + ", down[1]: " + d_fd(down[1]));

    fd_send_to_parent = up[1];

    if (!(flags & O_NONBLOCK)) {
      /* The supervisor needs nonblocking fds for the pipes. */
      fcntl(up[0], F_SETFL, flags | O_NONBLOCK);
      fcntl(down[1], F_SETFL, flags | O_NONBLOCK);
    }

#ifdef __clang_analyzer__
    /* Scan-build reports a false leak for the correct code. This is used only in static
     * analysis. It is broken because all shared pointers to the Pipe must be copies of
     * the shared self pointer stored in it. */
    auto pipe = std::make_shared<Pipe>(down[1] /* server fd */, unix_parent);
#else
    auto pipe = (new Pipe(down[1] /* server fd */, unix_parent))->shared_ptr();
#endif

    /* The reading side of this pipe is in the forked and the execed processes.
     * We're lazy and we don't register it for the forked process, no one cares. */
    auto ffd0 = std::make_shared<FileFD>((flags & ~O_ACCMODE) | O_RDONLY,
                                         pipe->fd0_shared_ptr(),
                                         unix_parent /* creator */,
                                         false /* close_on_popen */);
    proc->add_filefd(STDIN_FILENO /* client fd */, ffd0);

    /* The (so far only) writing side of this pipe is in the popening (parent) process. */
    auto ffd1 = std::make_shared<FileFD>((flags & ~O_ACCMODE) | O_WRONLY,
                                         pipe->fd1_shared_ptr(),
                                         unix_parent /* creator */,
                                         true /* close_on_popen */);
    unix_parent->add_filefd(pending_popen->fd /* client fd */, ffd1);

    auto recorders = std::vector<std::shared_ptr<PipeRecorder>>();
    pipe->add_fd1_and_proc(up[0] /* server fd */, ffd1.get(), proc, recorders);

    /* This is an incoming pipe in the child process that needs to be reopened because we
     * couldn't catch the pipe() call inside popen() and thus we couldn't do it yet.
     * Add this to the "reopen_fd_fifos" array of "scproc_resp", and to the ancillary data. */
    fd0_reopen = down[0];
  }

  /* ACK the parent, using a "popen_fd" message with the fd attached as ancillary data.
   * Then close that fd. */
  FBBCOMM_Builder_popen_fd msg;
  send_fbb(pending_popen->parent_conn, pending_popen->ack_num,
      reinterpret_cast<FBBCOMM_Builder *>(&msg), &fd_send_to_parent, 1);
  close(fd_send_to_parent);

  MessageProcessor::accept_exec_child(proc, pending_popen->child_conn, fd0_reopen);

  proc_tree->DropPendingPopen(unix_parent);
  unix_parent->set_has_pending_popen(false);
}

static void accept_fork_child(Process* parent, int parent_fd, int parent_ack,
                              Process** child_ref, int pid, int child_fd,
                              int child_ack) {
  TRACK(FB_DEBUG_PROC,
        "parent_fd=%s, parent_ack=%d, parent=%s pid=%d child_fd=%s child_ack=%d",
        D_FD(parent_fd), parent_ack, D(parent), pid, D_FD(child_fd), child_ack);

  auto proc = ProcessFactory::getForkedProcess(pid, parent);
  proc_tree->insert(proc);
  *child_ref = proc;
  ack_msg(parent_fd, parent_ack);
  ack_msg(child_fd, child_ack);
}

/**
 * Process message coming from interceptor
 * @param fb_conn file descriptor of the connection
 */
static void proc_new_process_msg(const FBBCOMM_Serialized *fbbcomm_buf, uint16_t ack_id,
                                 int fd_conn, Process** new_proc) {
  TRACK(FB_DEBUG_PROC, "fd_conn=%s, ack_id=%d", D_FD(fd_conn), ack_id);

  int tag = fbbcomm_buf->get_tag();
  if (tag == FBBCOMM_TAG_scproc_query) {
    auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_scproc_query *>(fbbcomm_buf);
    auto pid = ic_msg->get_pid();
    auto ppid = ic_msg->get_ppid();
    const char* ic_version = ic_msg->get_version();

    if (ic_version && strcmp(ic_version, FIREBUILD_VERSION) != 0) {
      fb_error("Mismatched interceptor version: " + std::string(ic_version));
      abort();
    }

    Process *unix_parent = NULL;
    LaunchType launch_type = LAUNCH_TYPE_OTHER;
    int type_flags;

    Process *parent = NULL;
    std::vector<std::shared_ptr<FileFD>>* fds = nullptr;

    /* Locate the parent in case of execve or alike. This includes the
     * case when the outermost intercepted process starts up (no
     * parent will be found) or when this outermost process does an
     * exec (an exec parent will be found then). */
    parent = proc_tree->pid2proc(pid);

    if (parent) {
      /* This PID was already seen, i.e. this process is the result of an exec*(),
       * or a posix_spawn*() where we've already seen and processed the
       * "posix_spawn_parent" message. */
      assert_cmp(parent->state(), !=, FB_PROC_FINALIZED);
      if (parent->state() == FB_PROC_TERMINATED) {
        fds = parent->pass_on_fds();
      } else {
        /* Queue the ExecedProcess until parent's connection is closed */
        fds = new std::vector<std::shared_ptr<FileFD>>();
        auto proc =
            ProcessFactory::getExecedProcess(
                ic_msg, parent, fds);
        proc_tree->QueueExecChild(parent->pid(), fd_conn, proc);
        *new_proc = proc;
        return;
      }
    } else if (ppid == getpid()) {
      /* This is the first intercepted process. */
      parent = proc_tree->root();
      fds = parent->pass_on_fds();
    } else {
      /* Locate the parent in case of system/popen/posix_spawn, but not
       * when the first intercepter process starts up. */
      unix_parent = proc_tree->pid2proc(ppid);
      if (!unix_parent) {
        /* The parent could not be found. There could be one or more statically linked binaries in
         * the exec() - fork() chain. There is not much the supervisor can do, with so much missing
         * information. Let the child continue unintercepted and notice the missing popen/system()
         * child later. */
        reject_exec_child(fd_conn);
        return;
      }

      /* Verify that the child was expected and get inherited fds. */
      std::vector<std::string> args = ic_msg->get_arg_as_vector();
      fds = unix_parent->pop_expected_child_fds(args, &launch_type, &type_flags);
      if (!fds) {
        fds = new std::vector<std::shared_ptr<FileFD>>();
      }

      if (unix_parent->posix_spawn_pending()) {
        /* This is a posix_spawn*() child, but we haven't yet seen and processed the
         * "posix_spawn_parent" message. Defer processing the child until "posix_spawn_parent"
         * is processed first.
         * Don't set the parent yet because we haven't created that ForkedProcess object yet.
         * Also don't set fds, we couldn't because that depends on the file actions. We'll
         * set these when handling the "posix_spawn_parent" message. */
        auto proc =
            ProcessFactory::getExecedProcess(
                ic_msg, nullptr, nullptr);
        proc_tree->QueuePosixSpawnChild(ppid, fd_conn, proc);
        *new_proc = proc;
        delete fds;
        return;
      }

      /* This is a system or popen child. */

      /* Add a ForkedProcess for the forked child we never directly saw. */
      parent = new ForkedProcess(pid, ppid, unix_parent, fds);

      if (launch_type == LAUNCH_TYPE_POPEN) {
        /* The new exec child should not inherit the fd connected to the unix_parent's popen()-ed
         * stream. The said fd is not necessarily open. */
        int child_fileno = is_wronly(type_flags) ? STDIN_FILENO : STDOUT_FILENO;
        parent->handle_force_close(child_fileno);

        /* The new exec child also does not inherit parent's popen()-ed fds.
         * See: glibc/libio/iopopen.c:
         *  POSIX states popen shall ensure that any streams from previous popen()
         *  calls that remain open in the parent process should be closed in the new
         *  child process. [...] */
        int fds_size = parent->fds()->size();
        for (int fd = 0; fd < fds_size; fd++) {
          const FileFD* file_fd = parent->get_fd(fd);
          if (file_fd && file_fd->close_on_popen()) {
            parent->handle_close(fd);
          }
        }
      }
      /* For the intermediate ForkedProcess where posix_spawn()'s file_actions were executed,
       * we still had all the fds, even the close-on-exec ones. Now it's time to close them. */
      fds = parent->pass_on_fds();

      parent->set_state(FB_PROC_TERMINATED);
      proc_tree->insert(parent);

      /* Now we can ack the previous posix_spawn()'s second message. */
      if (launch_type == LAUNCH_TYPE_POSIX_SPAWN) {
        proc_tree->AckParent(unix_parent->pid());
      }
    }

    /* Add the ExecedProcess. */
    auto proc =
        ProcessFactory::getExecedProcess(
            ic_msg, parent, fds);
    if (launch_type == LAUNCH_TYPE_SYSTEM) {
      unix_parent->set_system_child(proc);
    } else if (launch_type == LAUNCH_TYPE_POPEN) {
      /* Entry must have been created at the "popen" message */
      pending_popen_t *pending_popen = proc_tree->Proc2PendingPopen(unix_parent);
      assert(pending_popen);
      /* Fill in the new fields */
      assert_null(pending_popen->child);
      pending_popen->child = proc;
      pending_popen->child_conn = fd_conn;
      /* If the "popen_parent" message has already arrived then accept the popened child,
       * which will also ACK the parent.
       * Otherwise this will be done whenever the "popen_parent" message arrives. */
      if (pending_popen->fd >= 0) {
        accept_popen_child(unix_parent, pending_popen);
      }
      *new_proc = proc;
      return;
    }
    MessageProcessor::accept_exec_child(proc, fd_conn);
    *new_proc = proc;

  } else if (tag == FBBCOMM_TAG_fork_child) {
    auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_fork_child *>(fbbcomm_buf);
    auto pid = ic_msg->get_pid();
    auto ppid = ic_msg->get_ppid();
    auto pending_ack = proc_tree->PPid2ParentAck(ppid);
    /* The supervisor needs up to date information about the fork parent in the ProcessTree
     * when the child Process is created. To ensure having up to date information all the
     * messages must be processed from the fork parent up to ForkParent and only then can
     * the child Process created in the ProcessTree and let the child process continue execution.
     */
    if (!pending_ack) {
      /* queue fork_child data and delay processing messages on this socket */
      proc_tree->QueueForkChild(pid, fd_conn, ppid, ack_id, new_proc);
    } else {
      auto pproc = proc_tree->pid2proc(ppid);
      assert(pproc);
      /* record new process */
      accept_fork_child(pproc, pending_ack->sock, pending_ack->ack_num,
                        new_proc, pid, fd_conn, ack_id);
      proc_tree->DropParentAck(ppid);
    }
  }
}

static void posix_spawn_preopen_files(const FBBCOMM_Serialized_posix_spawn * const ic_msg,
                                      Process* const proc) {
  for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
    const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
    switch (action->get_tag()) {
      case FBBCOMM_TAG_posix_spawn_file_action_open: {
        /* A successful open to a particular fd, silently closing the previous file if any. */
        const FBBCOMM_Serialized_posix_spawn_file_action_open *action_open =
            reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
        int flags = action_open->get_flags();
        if (is_write(flags)) {
          const FileName* file_name = proc->get_absolute(
              AT_FDCWD, action_open->get_pathname(), action_open->get_pathname_len());
          if (file_name) {
            /* Pretend that the parent opened the file for writing and not the fork child.
             * This is not accurate, but the fork child does not exist yet. A parallel
             * process opening the file for writing would disable shortcutting the same way. */
            file_name->open_for_writing(proc->exec_point());
          }
        }
        break;
      }
      default:
        /* Only opens are handled (as pre_opens). */
        break;
    }
  }
}

template<class P>
static void process_posix_spawn_file_actions(const P * const ic_msg, Process* const proc) {
#ifdef __APPLE__
      std::unordered_set<int> new_fds {};
      const int attr_flags = ic_msg->get_attr_flags_with_fallback(0);
#endif
      for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
        const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
        switch (action->get_tag()) {
          case FBBCOMM_TAG_posix_spawn_file_action_open: {
            /* A successful open to a particular fd, silently closing the previous file if any. */
            auto action_open =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
            const char *pathname = action_open->get_pathname();
            const size_t pathname_len = action_open->get_pathname_len();
            int fd = action_open->get_fd();
#ifdef __APPLE__
            if (attr_flags & POSIX_SPAWN_CLOEXEC_DEFAULT) {
              new_fds.insert(fd);
            }
#endif
            int flags = action_open->get_flags();
            mode_t mode = action_open->get_mode();
            proc->handle_force_close(fd);
            proc->handle_open(AT_FDCWD, pathname, pathname_len, flags, mode, fd, 0, -1, 0,
                                    false, false);
            /* Revert the effect of "pre-opening" paths to be written in the posix_spawn message.*/
            if (is_write(flags)) {
              const FileName* file_name = proc->get_absolute(AT_FDCWD, pathname,
                                                                              pathname_len);
              if (file_name) {
                file_name->close_for_writing();
              }
            }
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_close: {
            /* A close attempt, maybe successful, maybe failed, we don't know. See glibc's
             * sysdeps/unix/sysv/linux/spawni.c:
             *   Signal errors only for file descriptors out of range.
             * sysdeps/posix/spawni.c:
             *   Only signal errors for file descriptors out of range.
             * whereas signaling the error means to abort posix_spawn and thus not reach
             * this code here. */
            auto action_close =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_close *>(action);
            int fd = action_close->get_fd();
            proc->handle_force_close(fd);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_closefrom: {
            /* A successful closefrom. */
            auto action_closefrom =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_closefrom *>
                (action);
            int lowfd = action_closefrom->get_lowfd();
            proc->handle_closefrom(lowfd);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_dup2: {
            /* A successful dup2.
             * Note that as per https://austingroupbugs.net/view.php?id=411 and glibc's
             * implementation, oldfd==newfd clears the close-on-exec bit (here only,
             * not in a real dup2()). */
            auto action_dup2 =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_dup2 *>(action);
            int oldfd = action_dup2->get_oldfd();
            int newfd = action_dup2->get_newfd();
            if (oldfd == newfd) {
              proc->handle_clear_cloexec(oldfd);
            } else {
              proc->handle_dup3(oldfd, newfd, 0, 0);
            }
#ifdef __APPLE__
            if (attr_flags & POSIX_SPAWN_CLOEXEC_DEFAULT) {
              new_fds.insert(newfd);
            }
#endif
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_chdir: {
            /* A successful chdir. */
            auto action_chdir =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_chdir *>(action);
            const char *pathname = action_chdir->get_pathname();
            proc->handle_set_wd(pathname);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_fchdir: {
            /* A successful fchdir. */
            auto action_fchdir =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_fchdir *>(action);
            int fd = action_fchdir->get_fd();
            proc->handle_set_fwd(fd);
            break;
          }
#ifdef __APPLE__
          case FBBCOMM_TAG_posix_spawn_file_action_inherit: {
            /* A successful inherit. */
            auto action_inherit =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_inherit *>(
                    action);
            int fd = action_inherit->get_fd();
            if (attr_flags & POSIX_SPAWN_CLOEXEC_DEFAULT) {
              new_fds.insert(fd);
            }
            proc->handle_clear_cloexec(fd);
            break;
          }
#endif
          default:
            assert(false);
        }
      }
#ifdef __APPLE__
      if (attr_flags & POSIX_SPAWN_CLOEXEC_DEFAULT) {
        for (size_t fd = 0; fd < proc->fds()->size(); fd++) {
          if (!new_fds.contains(fd)) {
            proc->handle_force_close(fd);
          }
        }
      }
#endif
}

static void proc_ic_msg(const FBBCOMM_Serialized *fbbcomm_buf, uint16_t ack_num,
                        int fd_conn, Process* proc) {
  TRACKX(FB_DEBUG_COMM, 1, 1, Process, proc, "fd_conn=%s, tag=%s, ack_num=%d",
         D_FD(fd_conn), fbbcomm_tag_to_string(fbbcomm_buf->get_tag()), ack_num);

  int tag = fbbcomm_buf->get_tag();
  assert(proc);
  switch (tag) {
    case FBBCOMM_TAG_fork_parent: {
      auto parent_pid = proc->pid();
      auto fork_child_sock = proc_tree->Pid2ForkChildSock(parent_pid);
      if (!fork_child_sock) {
        /* wait for child */
        proc_tree->QueueParentAck(parent_pid, ack_num, fd_conn);
      } else {
        /* record new child process */
        accept_fork_child(proc, fd_conn, ack_num,
                          fork_child_sock->fork_child_ref, fork_child_sock->child_pid,
                          fork_child_sock->sock, fork_child_sock->ack_num);
        proc_tree->DropQueuedForkChild(parent_pid);
      }
      return;
    }
    case FBBCOMM_TAG_exec_failed: {
      // FIXME(rbalint) check exec parameter and record what needs to be
      // checked when shortcutting the process
      proc->set_exec_pending(false);
      break;
    }
    case FBBCOMM_TAG_rusage: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_rusage *>(fbbcomm_buf);
      proc->resource_usage(ic_msg->get_utime_u(), ic_msg->get_stime_u());
      break;
    }
    case FBBCOMM_TAG_system: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_system *>(fbbcomm_buf);
      assert_null(proc->system_child());
      /* system(cmd) launches a child of argv = ["sh", "-c", "--", cmd], with "--" being optional */
      auto expected_child = new ExecedProcessEnv(proc->pass_on_fds(false), LAUNCH_TYPE_SYSTEM);
      if (ic_msg->has_cmd()) {
        expected_child->set_sh_c_command(ic_msg->get_cmd());
#ifdef __APPLE__
      } else {
        /* OS X's system(NULL) just checks /bin/sh instead of running it. */
        break;
#endif
      }
      proc->set_expected_child(expected_child);
      break;
    }
    case FBBCOMM_TAG_system_ret: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_system_ret *>(fbbcomm_buf);
#ifdef __APPLE__
      /* OS X's system(NULL) just checks /bin/sh instead of running it. */
      if (!ic_msg->has_cmd()) {
        break;
      }
#endif
      assert(proc->system_child());
      /* system() implicitly waits for the child to finish. */
      int ret = ic_msg->get_ret();
      if (ret == -1 || !WIFEXITED(ret)) {
        proc->system_child()->exec_point()->disable_shortcutting_bubble_up_to_excl(
            proc->system_child()->fork_point()->exec_point(),
            "Process started by system() exited abnormally or the exit status could not be"
            " collected");
      } else {
        proc->system_child()->fork_point()->set_exit_status(WEXITSTATUS(ret));
      }
      proc->system_child()->set_been_waited_for();
      if (!proc->system_child()->fork_point()->can_ack_parent_wait()) {
        /* The process has actually quit (otherwise the interceptor
         * couldn't send us the system_ret message), but the supervisor
         * hasn't seen this event yet. Thus we have to slightly defer
         * sending the ACK. */
        proc->system_child()->set_on_finalized_ack(ack_num, fd_conn);
        proc->set_system_child(NULL);
        return;
      }
      /* Can be ACK'd straight away. */
      proc->set_system_child(NULL);
      break;
    }
    case FBBCOMM_TAG_popen: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_popen *>(fbbcomm_buf);
      assert(proc_tree->Proc2PendingPopen(proc) == nullptr);

      int type_flags = ic_msg->get_type_flags();
      auto fds = proc->pass_on_fds(false);
      /* popen(cmd) launches a child of argv = ["sh", "-c", "--", cmd], with "--" being optional */
      auto expected_child = new ExecedProcessEnv(fds, LAUNCH_TYPE_POPEN);
      // FIXME what if !has_cmd() ?
      expected_child->set_sh_c_command(ic_msg->get_cmd());
      expected_child->set_type_flags(type_flags);
      proc->set_expected_child(expected_child);

      pending_popen_t pending_popen;
      pending_popen.type_flags = type_flags;  // FIXME why set it at two places?
      proc_tree->QueuePendingPopen(proc, pending_popen);
      proc->set_has_pending_popen(true);
      break;
    }
    case FBBCOMM_TAG_popen_parent: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_popen_parent *>(fbbcomm_buf);
      /* Entry must have been created at the "popen" message */
      pending_popen_t *pending_popen = proc_tree->Proc2PendingPopen(proc);
      assert(pending_popen);
      /* Fill in the new fields */
      assert(pending_popen->fd == -1);
      pending_popen->fd = ic_msg->get_fd();
      pending_popen->parent_conn = fd_conn;
      pending_popen->ack_num = ack_num;
      /* If the child's "scproc_query" message has already arrived then accept the popened child,
       * which will also ACK the parent.
       * Otherwise this will be done whenever the child's "scproc_query" message arrives.*/
      if (pending_popen->child) {
        accept_popen_child(proc, pending_popen);
      }
      return;
    }
    case FBBCOMM_TAG_popen_failed: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_popen_failed *>(fbbcomm_buf);
      // FIXME what if !has_cmd() ?
      delete(proc->pop_expected_child_fds(
          std::vector<std::string>({"sh", "-c", "--", ic_msg->get_cmd()}),
          nullptr, nullptr, true));
      break;
    }
    case FBBCOMM_TAG_pclose: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_pclose *>(fbbcomm_buf);
      if (!ic_msg->has_error_no()) {
        /* pclose() is essentially an fclose() first, then a waitpid(), but the interceptor
         * sends an extra close message in advance thus here the fd is already tracked as closed. */
        ExecedProcess *child =
            proc->PopPopenedProcess(ic_msg->get_fd());
        assert(child);
        int ret = ic_msg->get_ret();
        if (ret == -1 || !WIFEXITED(ret)) {
          child->exec_point()->disable_shortcutting_bubble_up_to_excl(
              child->fork_point()->exec_point(),
              "Process started by popen() exited abnormally or the exit status could not be"
              " collected");
        } else {
          child->fork_point()->set_exit_status(WEXITSTATUS(ret));
        }
        child->set_been_waited_for();
        if (!child->fork_point()->can_ack_parent_wait()) {
          /* We haven't seen the process quitting yet. Defer sending the ACK. */
          child->set_on_finalized_ack(ack_num, fd_conn);
          return;
        }
        /* Else we can ACK straight away. */
      }
      break;
    }
    case FBBCOMM_TAG_posix_spawn: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_posix_spawn *>(fbbcomm_buf);
#ifdef __APPLE__
      const int attr_flags = ic_msg->get_attr_flags_with_fallback(0);
      if (attr_flags & POSIX_SPAWN_SETEXEC) {
        proc->update_rusage(ic_msg->get_utime_u(), ic_msg->get_stime_u());
        // FIXME(rbalint) save parameters of pending exec()-ed process
        process_posix_spawn_file_actions<FBBCOMM_Serialized_posix_spawn>(ic_msg, proc);
        proc->set_exec_pending(true);
        break;
      }
#endif
      auto expected_child = new ExecedProcessEnv(proc->pass_on_fds(false), LAUNCH_TYPE_POSIX_SPAWN);
      std::vector<std::string> argv = ic_msg->get_arg_as_vector();
      expected_child->set_argv(argv);
      proc->set_expected_child(expected_child);
      proc->set_posix_spawn_pending(true);
      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Pre-open the files to be written. */
      posix_spawn_preopen_files(ic_msg, proc);
      break;
    }
    case FBBCOMM_TAG_posix_spawn_parent: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_parent *>(fbbcomm_buf);
      /* First, do the basic fork() */
      auto pid = ic_msg->get_pid();
      auto fork_child = ProcessFactory::getForkedProcess(pid, proc);
      proc_tree->insert(fork_child);

      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Do the corresponding administration. */
      process_posix_spawn_file_actions<FBBCOMM_Serialized_posix_spawn_parent>(ic_msg, fork_child);
      proc->set_posix_spawn_pending(false);

      auto posix_spawn_child_sock = proc_tree->Pid2PosixSpawnChildSock(proc->pid());
      if (posix_spawn_child_sock) {
        /* The child has already appeared, but had to wait for this "posix_spawn_parent" message.
         * Let the child continue (respond to the pending "scproc_query" with "scproc_resp"). */
        auto posix_spawn_child = posix_spawn_child_sock->incomplete_child;
        fork_child->set_exec_child(posix_spawn_child);
        posix_spawn_child->set_parent(fork_child);
        posix_spawn_child->set_fds(fork_child->pass_on_fds());
        MessageProcessor::accept_exec_child(posix_spawn_child, posix_spawn_child_sock->sock);
        proc_tree->DropQueuedPosixSpawnChild(proc->pid());
      } else {
        /* The child hasn't appeared yet. Register a pending exec, just like we do at exec*()
         * calls. This lets us detect a statically linked binary launched by posix_spawn(),
         * exactly the way we do at a regular exec*(), i.e. successfully wait*()ing for a child
         * that is in exec_pending state. */
        std::vector<std::string> arg = ic_msg->get_arg_as_vector();
        delete(proc->pop_expected_child_fds(arg, nullptr));
        fork_child->set_exec_pending(true);
      }
      fork_child->set_state(FB_PROC_TERMINATED);
      /* In either case, ACK the "posix_spawn_parent" message, don't necessarily wait for the
       * child to appear. */
      break;
    }
    case FBBCOMM_TAG_posix_spawn_failed: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_failed *>(fbbcomm_buf);
      std::vector<std::string> arg = ic_msg->get_arg_as_vector();
      delete(proc->pop_expected_child_fds(arg, nullptr, nullptr, true));
      proc->set_posix_spawn_pending(false);
      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Revert the pre-opening of the files to be written. */
      for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
        const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
        switch (action->get_tag()) {
          case FBBCOMM_TAG_posix_spawn_file_action_open: {
            /* A successful open to a particular fd, silently closing the previous file if any. */
            auto action_open =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
            int flags = action_open->get_flags();
            if (is_write(flags)) {
              const FileName* file_name = proc->get_absolute(
                  AT_FDCWD, action_open->get_pathname(), action_open->get_pathname_len());
              if (file_name) {
                file_name->close_for_writing();
              }
            }
            break;
          }
          default:
            /* Only opens are handled (as pre_opens). */
            break;
        }
      }
      break;
    }
    case FBBCOMM_TAG_wait: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_wait *>(fbbcomm_buf);
      const int pid = ic_msg->get_pid();
      Process *child = proc_tree->pid2proc(pid);
      assert(child);
      int status;
      bool exited;

      if (ic_msg->has_si_code()) {
        /* The intercepted call was waitid() actually. */
        status = ic_msg->get_si_status();
        exited = ic_msg->get_si_code() == CLD_EXITED;
      } else {
        const int wstatus = ic_msg->get_wstatus();
        status = WEXITSTATUS(wstatus);
        exited = WIFEXITED(wstatus);
      }
      if (exited) {
        child->fork_point()->set_exit_status(status);
      } else {
        child->exec_point()->disable_shortcutting_bubble_up_to_excl(
            child->fork_point()->exec_point(),
            "Process exited abnormally");
      }

      child->set_been_waited_for();
      if (child->exec_pending()) {
        /* If the supervisor believes an exec is pending in a child proces while the parent
         * actually successfully waited for the child, it means that the child didn't sign in to
         * the supervisor, presumably because it is statically linked. See #324 for details. */
        child->exec_point()->disable_shortcutting_bubble_up(
            "Process did not sign in to supervisor, perhaps statically linked or failed to link");
        /* Need to also clear the exec_pending state for Process::any_child_not_finalized()
         * and finalize this never-seen process. */
        child->set_exec_pending(false);
        child->reset_file_fd_pipe_refs();
        child->maybe_finalize();
        /* Ack it straight away. */
      } else if (!child->fork_point()->can_ack_parent_wait()) {
        /* We haven't seen the process quitting yet. Defer sending the ACK. */
        child->set_on_finalized_ack(ack_num, fd_conn);
        return;
      }
      /* Else we can ACK straight away. */
      break;
    }
    case FBBCOMM_TAG_pipe_request: {
      ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_pipe_request *>(fbbcomm_buf), fd_conn);
      break;
    }
    case FBBCOMM_TAG_pipe_fds: {
      PFBBA_HANDLE(proc, pipe_fds, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_exec: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_exec *>(fbbcomm_buf);
      proc->update_rusage(ic_msg->get_utime_u(), ic_msg->get_stime_u());
      // FIXME(rbalint) save exec parameters
      proc->set_exec_pending(true);
      break;
    }
    case FBBCOMM_TAG_pre_open: {
      PFBBA_HANDLE(proc, pre_open, fbbcomm_buf);
     break;
    }
    case FBBCOMM_TAG_open: {
      PFBBA_HANDLE_ACKED(proc, open, fbbcomm_buf, fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_freopen: {
      PFBBA_HANDLE_ACKED(proc, freopen, fbbcomm_buf, fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_dlopen: {
      PFBBA_HANDLE_ACKED(proc, dlopen, fbbcomm_buf, fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_close: {
      PFBBA_HANDLE(proc, close, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_closefrom: {
      PFBBA_HANDLE(proc, closefrom, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_close_range: {
      PFBBA_HANDLE(proc, close_range, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_scandirat: {
      PFBBA_HANDLE(proc, scandirat, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_truncate: {
      PFBBA_HANDLE(proc, truncate, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_unlink: {
      PFBBA_HANDLE(proc, unlink, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_mkdir: {
      PFBBA_HANDLE(proc, mkdir, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_rmdir: {
      PFBBA_HANDLE(proc, rmdir, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_dup3: {
      PFBBA_HANDLE(proc, dup3, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_dup: {
      PFBBA_HANDLE(proc, dup, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_rename: {
      PFBBA_HANDLE(proc, rename, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_symlink: {
      PFBBA_HANDLE(proc, symlink, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_fcntl: {
      PFBBA_HANDLE(proc, fcntl, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_ioctl: {
      PFBBA_HANDLE(proc, ioctl, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_umask: {
      PFBBA_HANDLE(proc, umask, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_chdir: {
      PFBBA_HANDLE(proc, chdir, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_fchdir: {
      PFBBA_HANDLE(proc, fchdir, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_read_from_inherited: {
      PFBBA_HANDLE(proc, read_from_inherited, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_write_to_inherited: {
      PFBBA_HANDLE(proc, write_to_inherited, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_seek_in_inherited: {
      PFBBA_HANDLE(proc, seek_in_inherited, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_inherited_fd_offset: {
      PFBBA_HANDLE(proc, inherited_fd_offset, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_recvmsg_scm_rights: {
      PFBBA_HANDLE(proc, recvmsg_scm_rights, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_link: {
      proc->exec_point()->disable_shortcutting_bubble_up("Creating a hard link is not supported");
      break;
    }
    case FBBCOMM_TAG_fstatat: {
      PFBBA_HANDLE(proc, fstatat, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_faccessat: {
      PFBBA_HANDLE(proc, faccessat, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_fchmodat: {
      PFBBA_HANDLE(proc, fchmodat, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_shm_open: {
      PFBBA_HANDLE(proc, shm_open, fbbcomm_buf);
      break;
    }
#ifdef __APPLE__
    case FBBCOMM_TAG_kqueue: {
      PFBBA_HANDLE(proc, kqueue, fbbcomm_buf);
      break;
    }
#endif
#ifdef __linux__
    case FBBCOMM_TAG_memfd_create: {
      PFBBA_HANDLE(proc, memfd_create, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_timerfd_create: {
      PFBBA_HANDLE(proc, timerfd_create, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_epoll_create: {
      PFBBA_HANDLE(proc, epoll_create, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_eventfd: {
      PFBBA_HANDLE(proc, eventfd, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_signalfd: {
      PFBBA_HANDLE(proc, signalfd, fbbcomm_buf);
      break;
    }
#endif
    case FBBCOMM_TAG_getrandom: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_getrandom *>(fbbcomm_buf);
      const unsigned int flags = ic_msg->get_flags_with_fallback(0);
#ifdef GRND_RANDOM
      const std::string pathname(flags & GRND_RANDOM ? "/dev/random" : "/dev/urandom");
#else
      const std::string pathname("/dev/urandom");
      (void)flags;
#endif
      if (!FileName::Get(pathname)->is_in_ignore_location()) {
        proc->exec_point()->disable_shortcutting_bubble_up(
            deduplicated_string("Using " + pathname + " is not allowed").c_str());
      }
      break;
    }
    case FBBCOMM_TAG_futime: {
      auto ic_msg = reinterpret_cast<const FBBCOMM_Serialized_futime *>(fbbcomm_buf);
      const int fd = ic_msg->get_fd();
      const FileFD* ffd = proc->get_fd(fd);
      if (!ic_msg->has_error_no() && ffd && is_write(ffd->flags()) && ic_msg->get_all_utime_now()) {
        /* The fd has been opened for writing and the access and modification times should be set to
         * current time which happens automatically when the process is shortcut. This is safe. */
      } else {
        ExecedProcess* next_exec_level;
        if (quirks & FB_QUIRK_LTO_WRAPPER && proc->exec_point()->args().size() > 0
            && proc->exec_point()->args()[0] == "touch"
            && (next_exec_level = proc->parent_exec_point())  // sh
            && (next_exec_level = next_exec_level->parent_exec_point())  // make
            && (next_exec_level = next_exec_level->parent_exec_point())  // lto-wrapper
            && next_exec_level->executable()->without_dirs() == "lto-wrapper" ) {
          FB_DEBUG(FB_DEBUG_PROC, "Allow shortcutting lto-wrapper's touch descendant "
                   "(lto-wrapper quirk)");
        } else {
          proc->exec_point()->disable_shortcutting_bubble_up(
              "Changing file timestamps is not supported");
        }
      }
      break;
    }
    case FBBCOMM_TAG_utime: {
      proc->exec_point()->disable_shortcutting_bubble_up(
          "Changing file timestamps is not supported");
      break;
    }
    case FBBCOMM_TAG_clock_gettime: {
      if (quirks & FB_QUIRK_IGNORE_TIME_QUERIES) {
        FB_DEBUG(FB_DEBUG_PROC, "Allow shortcutting time query "
                 "(ignore-time-queries quirk)");
      } else {
        proc->exec_point()->disable_shortcutting_bubble_up(
            "Time queries such as clock_gettime() prevent shortcutting unless the "
            "\"ignore-time-queries\" quirk is set.");
      }
      break;
    }
    case FBBCOMM_TAG_clone: {
      proc->exec_point()->disable_shortcutting_bubble_up("clone() is not supported");
      break;
    }
    case FBBCOMM_TAG_socket: {
      PFBBA_HANDLE(proc, socket, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_socketpair: {
      PFBBA_HANDLE(proc, socketpair, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_connect: {
      PFBBA_HANDLE(proc, connect, fbbcomm_buf);
      break;
    }
    case FBBCOMM_TAG_statfs:
      ProcessFBBAdaptor::handle(
           proc, reinterpret_cast<const FBBCOMM_Serialized_statfs *>(fbbcomm_buf));
      break;
    case FBBCOMM_TAG_mktemp:
      ProcessFBBAdaptor::handle(
           proc, reinterpret_cast<const FBBCOMM_Serialized_mktemp *>(fbbcomm_buf));
      break;
    case FBBCOMM_TAG_gethostname:
      /* Ignore gethostname. With a local cache it should not make a difference, while
       * in a shared cache the intention is to use cached results from other machines. */
      break;
    case FBBCOMM_TAG_seccomp:
      /* Ignore seccomp(). The interposer always returns EINVAL error to keep interception
       * working. This breaks sandboxing, but builds can run arbitrary commands anyway. */
      break;
    case FBBCOMM_TAG_mac_syscall:
      /* Ignore __mac_syscall(). Having the tag defined allows easier debugging with -d comm. */
      // TODO(rbalint) check if any __mac_syscall() invocation could impact builds
      break;
    case FBBCOMM_TAG_fb_debug:
    case FBBCOMM_TAG_fb_error:
    case FBBCOMM_TAG_fchownat:
    case FBBCOMM_TAG_fpathconf:
    case FBBCOMM_TAG_getdomainname:
    case FBBCOMM_TAG_lockf:
    case FBBCOMM_TAG_pathconf:
    case FBBCOMM_TAG_readlink:
    case FBBCOMM_TAG_scproc_resp:
    case FBBCOMM_TAG_sysconf:
      {
      // TODO(rbalint)
      break;
    }
    case FBBCOMM_TAG_gen_call: {
      auto msg = reinterpret_cast<const FBBCOMM_Serialized_gen_call *>(fbbcomm_buf);
      const int error = msg->get_error_no_with_fallback(0);
      proc->exec_point()->disable_shortcutting_bubble_up(
          deduplicated_string(
              std::string(error == 0 ? "" : "failed") + msg->get_call() + " is not supported"
              + (error == 0 ? "" : " (error: " + d(error) + ")")).c_str());
      break;
    }
    default: {
      fb_error("Unknown FBB message tag: " + std::to_string(tag));
      assert(0 && "Unknown message FBB message tag!");
    }
  }

  if (ack_num != 0) {
    ack_msg(fd_conn, ack_num);
  }
} /* NOLINT(readability/fn_size) */

void MessageProcessor::ic_conn_readcb(const struct epoll_event* event, void *ctx) {
  auto conn_ctx = reinterpret_cast<ConnectionContext*>(ctx);
  auto proc = conn_ctx->proc;
  auto &buf = conn_ctx->buffer();
  size_t full_length;
  const msg_header * header;
  ProcessDebugSuppressor debug_suppressor(proc);

  if (!Epoll::ready_for_read(event)) {
    FB_DEBUG(FB_DEBUG_COMM, "socket " +
             d_fd(Epoll::event_fd(event)) +
             " hung up (" + d(proc) + ")");
    delete conn_ctx;
    return;
  }
  int read_ret = buf.read(Epoll::event_fd(event), -1);
  if (read_ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      /* Try again later. */
      return;
    }
  }
  if (read_ret <= 0) {
    FB_DEBUG(FB_DEBUG_COMM, "socket " +
             d_fd(Epoll::event_fd(event)) +
             " hung up (" + d(proc) + ")");
    delete conn_ctx;
    return;
  }

  do {
    if (buf.length() < sizeof(*header)) {
      /* Header is still incomplete, try again later. */
      return;
    } else {
      header = reinterpret_cast<const msg_header *>(buf.data());
      full_length = sizeof(*header) + header->msg_size;
      if (buf.length() < full_length) {
        /* Have partial message, more data is needed. */
        return;
      }
    }

    /* Have at least one full message. */
    auto fbbcomm_msg = reinterpret_cast<const FBBCOMM_Serialized *>(buf.data() + sizeof(*header));

    if (!proc) {
      /* Now the message is complete, the debug suppression can be correctly set. */
      debug_suppressed =
          ProcessFactory::peekProcessDebuggingSuppressed(fbbcomm_msg);
    }

    if (FB_DEBUGGING(FB_DEBUG_COMM)) {
      if (!debug_suppressed) {
        FB_DEBUG(FB_DEBUG_COMM, "fd " + d_fd(
            Epoll::event_fd(event)) + ": (" + d(proc) + ")");
        if (header->ack_id) {
          fprintf(stderr, "ack_num: %d\n", header->ack_id);
        }
        fbbcomm_msg->debug(stderr);
        fflush(stderr);
      }
    }

    /* Process the messaage. */
    if (proc) {
      proc_ic_msg(fbbcomm_msg, header->ack_id, Epoll::event_fd(event), proc);
    } else {
      /* Fist interceptor message */
      proc_new_process_msg(
          fbbcomm_msg, header->ack_id, Epoll::event_fd(event), &conn_ctx->proc);
      /* Reset suppression which was set peeking at the message. */
      debug_suppressed = false;
    }
    buf.discard(full_length);
  } while (buf.length() > 0);
}


}  /* namespace firebuild */
