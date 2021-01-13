/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include <event2/event.h>
#include <signal.h>
#include <flatbuffers/flatbuffers.h>
#include <getopt.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <fcntl.h>
#include <libgen.h>

#include <string>
#include <iostream>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <libconfig.h++>


#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/config.h"
#include "firebuild/blob_cache.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "firebuild/cache_object_format_generated.h"
#pragma GCC diagnostic pop
#include "firebuild/connection_context.h"
#include "firebuild/fd.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/obj_cache.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/process_factory.h"
#include "firebuild/process_tree.h"
#include "firebuild/process_proto_adaptor.h"
#include "firebuild/utils.h"
#include "./fbb.h"

/** global configuration */
libconfig::Config * cfg;
bool generate_report = false;

struct event_base * ev_base = NULL;

namespace {

static char datadir[] = FIREBUILD_DATADIR;

static char *fb_tmp_dir;
static char *fb_conn_string;

evutil_socket_t listener;
struct event *sigchild_event;

static int inherited_fd = -1;
static int child_pid, child_ret = 1;
static bool insert_trace_markers = false;
static const char *report_file = "firebuild-build-report.html";
static firebuild::ProcessTree *proc_tree;
static firebuild::ExecedProcessCacher *cacher;

static void usage() {
  printf("Usage: firebuild [OPTIONS] <BUILD COMMAND>\n"
         "Execute BUILD COMMAND with FireBuild™ instrumentation\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "   -c --config-file=FILE     use FILE as configuration file\n"
         "   -C --directory=DIR        change directory before running the command\n"
         "   -d --debug-flags=list     comma separated list of debug flags,\n"
         "                             \"-d help\" to get a list.\n"
         "   -r --generate-report[=HTML] generate a report on the build command execution.\n"
         "                             the report's filename can be specified \n"
         "                             (firebuild-build-report.html by default). \n"
         "   -h --help                 show this help\n"
         "   -o --option=key=val       Add or replace a scalar in the config\n"
         "   -o --option=key+=val      Append to an array of scalars in the config\n"
         "   -o --option=key-=val      Remove from an array of scalars in the config\n"
         "   -i --insert-trace-markers perform open(\"/FIREBUILD <debug_msg>\", 0) calls\n"
         "                             to let users find unintercepted calls using\n"
         "                             strace or ltrace\n"
         "Exit status:\n"
         " exit status of the BUILD COMMAND\n"
         " 1  in case of failure\n");
}

/**
 * Construct a NULL-terminated array of "NAME=VALUE" environment variables
 * for the build command. The returned stings and array must be free()-d.
 *
 * TODO: detect duplicates
 */
static char** get_sanitized_env() {
  const libconfig::Setting& root = cfg->getRoot();

  FB_DEBUG(firebuild::FB_DEBUG_PROC, "Passing through environment variables:");

  const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
  std::vector<std::string> env_v;
  for (int i = 0; i < pass_through.getLength(); i++) {
    char * got_env = getenv(pass_through[i].c_str());
    if (NULL  != got_env) {
      env_v.push_back(pass_through[i].c_str() +
                      std::string("=") +
                      std::string(got_env));
      FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + env_v.back());
    }
  }
  FB_DEBUG(firebuild::FB_DEBUG_PROC, "");

  FB_DEBUG(firebuild::FB_DEBUG_PROC, "Setting preset environment variables:");
  const libconfig::Setting& preset = root["env_vars"]["preset"];
  for (int i = 0; i < preset.getLength(); i++) {
    env_v.push_back(preset[i]);
    FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + env_v.back());
  }

  std::string system_locations;
  const libconfig::Setting& system_locations_setting = root["system_locations"];
  for (int i = 0; i < system_locations_setting.getLength(); i++) {
    std::string loc = system_locations_setting[i];
    if (system_locations.length() == 0) {
      system_locations.append(loc);
    } else {
      system_locations.append(":" + loc);
    }
  }
  if (system_locations.length() > 0) {
    env_v.push_back("FB_SYSTEM_LOCATIONS=" + std::string(system_locations));
    FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + env_v.back());
  }

  env_v.push_back("LD_PRELOAD=libfbintercept.so");
  env_v.push_back("FB_SOCKET=" + std::string(fb_conn_string));
  FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + env_v.back());

  FB_DEBUG(firebuild::FB_DEBUG_PROC, "");

  if (insert_trace_markers) {
    env_v.push_back("FB_INSERT_TRACE_MARKERS=1");
  }

  char ** ret_env =
      static_cast<char**>(malloc(sizeof(char*) * (env_v.size() + 1)));

  auto it = env_v.begin();
  int i = 0;
  while (it != env_v.end()) {
    ret_env[i] = strdup(it->c_str());
    it++;
    i++;
  }
  ret_env[i] = NULL;

  return ret_env;
}

/*
 * Check if either exe or arg0 matches any of the entries in the list.
 */
static bool exe_matches_list(const firebuild::FileName* exe_file,
                             const std::string& arg0,
                             const libconfig::Setting& list) {
  std::string exe = exe_file->to_string();
  size_t pos = exe.rfind('/');
  std::string exe_base = exe.substr(pos == std::string::npos ? 0 : pos + 1);
  pos = arg0.rfind('/');
  std::string arg0_base = arg0.substr(pos == std::string::npos ? 0 : pos + 1);

  for (int i = 0; i < list.getLength(); i++) {
    const std::string& entry = list[i];
    if (entry.find('/') == std::string::npos) {
      /* If the entry doesn't contain a '/', only the basename needs to match. */
      if (entry == exe_base || entry == arg0_base) {
        return true;
      }
    } else {
      /* If the entry contains a '/', it needs to be an exact string match. */
      if (entry == exe || entry == arg0) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

namespace firebuild {

void accept_exec_child(ExecedProcess* proc, FD fd_conn,
                       ProcessTree* proc_tree) {
    TRACK(FB_DEBUG_PROC, "proc=%s, fd_conn=%s", D(proc), D(fd_conn));

    FBB_Builder_scproc_resp sv_msg;
    fbb_scproc_resp_init(&sv_msg);

    proc_tree->insert(proc);
    proc->initialize();

    /* Check for executables that are known not to be shortcuttable. */
    if (exe_matches_list(proc->executable(),
                         proc->args().size() > 0 ? proc->args()[0] : "",
                         cfg->getRoot()["processes"]["blacklist"])) {
      proc->disable_shortcutting_bubble_up("Executable blacklisted");
    }

    /* If we still potentially can, and prefer to cache / shortcut this process,
     * register the cacher object and calculate the process's fingerprint. */
    if (proc->can_shortcut() &&
        !exe_matches_list(proc->executable(),
                          proc->args().size() > 0 ? proc->args()[0] : "",
                          cfg->getRoot()["processes"]["skip_cache"])) {
      proc->set_cacher(cacher);
      if (!cacher->fingerprint(proc)) {
        proc->disable_shortcutting_bubble_up("Could not fingerprint the process");
      }
    }

    /* Try to shortcut the process. */
    if (proc->shortcut()) {
      fbb_scproc_resp_set_shortcut(&sv_msg, true);
      fbb_scproc_resp_set_exit_status(&sv_msg, proc->exit_status());
    } else {
      fbb_scproc_resp_set_shortcut(&sv_msg, false);
      if (debug_flags != 0) {
        fbb_scproc_resp_set_debug_flags(&sv_msg, debug_flags);
      }
    }

    fbb_send(fd_conn.fd(), &sv_msg, 0);
}

}  // namespace firebuild

namespace {

static void accept_fork_child(firebuild::Process* parent, firebuild::FD parent_fd, int parent_ack,
                              firebuild::Process** child_ref, int pid, firebuild::FD child_fd,
                              int child_ack, firebuild::ProcessTree* proc_tree) {
  TRACK(firebuild::FB_DEBUG_PROC,
        "parent_fd=%s, parent_ack=%d, parent=%s pid=%d child_fd=%s child_ack=%d",
        D(parent_fd), parent_ack, D(parent), pid, D(child_fd), child_ack);

  auto proc = firebuild::ProcessFactory::getForkedProcess(pid, parent);
  proc_tree->insert(proc);
  *child_ref = proc;
  firebuild::ack_msg(parent_fd, parent_ack);
  firebuild::ack_msg(child_fd, child_ack);
}

/**
 * Process message coming from interceptor
 * @param fb_conn file desctiptor of the connection
 */
void proc_new_process_msg(const void *fbb_buf, uint32_t ack_id, firebuild::FD fd_conn,
                          firebuild::Process** new_proc) {
  TRACK(firebuild::FB_DEBUG_PROC, "fd_conn=%s, ack_id=%d", D(fd_conn), ack_id);

  int tag = *reinterpret_cast<const int *>(fbb_buf);
  if (tag == FBB_TAG_scproc_query) {
    const FBB_scproc_query *ic_msg = reinterpret_cast<const FBB_scproc_query *>(fbb_buf);
    auto pid = fbb_scproc_query_get_pid(ic_msg);
    auto ppid = fbb_scproc_query_get_ppid(ic_msg);

    ::firebuild::Process *unix_parent = NULL;
    firebuild::LaunchType launch_type = firebuild::LAUNCH_TYPE_OTHER;

    firebuild::Process *parent = NULL;
    std::shared_ptr<std::vector<std::shared_ptr<firebuild::FileFD>>> fds = nullptr;

    /* Locate the parent in case of execve or alike. This includes the
     * case when the outermost intercepted process starts up (no
     * parent will be found) or when this outermost process does an
     * exec (an exec parent will be found then). */
    parent = proc_tree->pid2proc(pid);

    if (parent) {
      /* This PID was already seen, i.e. this process is the result of an exec*(),
       * or a posix_spawn*() where we've already seen and processed the
       * "posix_spawn_parent" message. */
      assert(parent->state() != firebuild::FB_PROC_FINALIZED);
      if (parent->state() == firebuild::FB_PROC_TERMINATED) {
        fds = parent->pass_on_fds();
      } else {
        /* Queue the ExecedProcess until parent's connection is closed */
        auto proc =
            firebuild::ProcessFactory::getExecedProcess(
                ic_msg, parent, fds);
        proc_tree->QueueExecChild(parent->pid(), fd_conn, proc);
        *new_proc = proc;
        return;
      }
    } else if (ppid == getpid()) {
      /* This is the first intercepted process. Nothing to do here. */
    } else {
      /* Locate the parent in case of system/popen/posix_spawn, but not
       * when the first intercepter process starts up. */
      unix_parent = proc_tree->pid2proc(ppid);
      assert(unix_parent != NULL);

      /* Verify that the child was expected and get inherited fds. */
      std::vector<std::string> args = fbb_scproc_query_get_arg(ic_msg);
      fds = unix_parent->pop_expected_child_fds(args, &launch_type);

      if (unix_parent->posix_spawn_pending()) {
        /* This is a posix_spawn*() child, but we haven't yet seen and processed the
         * "posix_spawn_parent" message. Defer processing the child until "posix_spawn_parent"
         * is processed first.
         * Don't set the parent yet because we haven't created that ForkedProcess object yet.
         * Also don't set fds, we couldn't because that depends on the file actions. We'll
         * set these when handling the "posix_spawn_parent" message. */
        auto proc =
            firebuild::ProcessFactory::getExecedProcess(
                ic_msg, nullptr, nullptr);
        proc_tree->QueuePosixSpawnChild(ppid, fd_conn, proc);
        *new_proc = proc;
        return;
      }

      /* This is a system or popen child. */

      /* Add a ForkedProcess for the forked child we never directly saw. */
      parent = new firebuild::ForkedProcess(pid, ppid, unix_parent, fds);

      /* For the intermediate ForkedProcess where posix_spawn()'s file_actions were executed,
       * we still had all the fds, even the close-on-exec ones. Now it's time to close them. */
      fds = parent->pass_on_fds();

      parent->set_state(firebuild::FB_PROC_TERMINATED);
      // FIXME set exec_child_ ???
      proc_tree->insert(parent);

      /* Now we can ack the previous popen()/posix_spawn()'s second message. */
      auto pending_ack = proc_tree->PPid2ParentAck(unix_parent->pid());
      if (pending_ack) {
        firebuild::ack_msg(pending_ack->sock, pending_ack->ack_num);
        proc_tree->DropParentAck(unix_parent->pid());
      }
    }

    /* Add the ExecedProcess. */
    auto proc =
        firebuild::ProcessFactory::getExecedProcess(
            ic_msg, parent, fds);
    if (launch_type == firebuild::LAUNCH_TYPE_SYSTEM) {
      unix_parent->set_system_child(proc);
    } else if (launch_type == firebuild::LAUNCH_TYPE_POPEN) {
      if (unix_parent->pending_popen_fd() != -1) {
        /* The popen_parent message has already arrived. Take a note of the fd -> child mapping. */
        assert(!unix_parent->pending_popen_child());
        unix_parent->AddPopenedProcess(unix_parent->pending_popen_fd(), proc);
        unix_parent->set_pending_popen_fd(-1);
      } else {
        /* The popen_parent message has not yet arrived.
         * Remember the new child, the handler of the popen_parent message will need it. */
        assert(!unix_parent->pending_popen_child());
        unix_parent->set_pending_popen_child(proc);
      }
    }
    accept_exec_child(proc, fd_conn, proc_tree);
    *new_proc = proc;

  } else if (tag == FBB_TAG_fork_child) {
    const FBB_fork_child *ic_msg = reinterpret_cast<const FBB_fork_child *>(fbb_buf);
    auto pid = fbb_fork_child_get_pid(ic_msg);
    auto ppid = fbb_fork_child_get_ppid(ic_msg);
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
                        new_proc, pid, fd_conn, ack_id, proc_tree);
      proc_tree->DropParentAck(ppid);
    }
  }
}

void proc_ic_msg(const void *fbb_buf,
                 uint32_t ack_num,
                 firebuild::FD fd_conn,
                 firebuild::Process* proc) {
  TRACK(firebuild::FB_DEBUG_COMM, "fd_conn=%s, tag=%s, ack_num=%d, proc=%s",
        D(fd_conn), fbb_tag_string(fbb_buf), ack_num, D(proc));

  int tag = *reinterpret_cast<const int *>(fbb_buf);
  assert(proc);
  switch (tag) {
    case FBB_TAG_fork_parent: {
      const FBB_fork_parent *ic_msg = reinterpret_cast<const FBB_fork_parent *>(fbb_buf);
      auto child_pid = fbb_fork_parent_get_pid(ic_msg);
      auto fork_child_sock = proc_tree->Pid2ForkChildSock(child_pid);
      if (!fork_child_sock) {
        /* wait for child */
        proc_tree->QueueParentAck(proc->pid(), ack_num, fd_conn);
      } else {
        /* record new child process */
        accept_fork_child(proc, fd_conn, ack_num,
                          fork_child_sock->fork_child_ref, child_pid, fork_child_sock->sock,
                          fork_child_sock->ack_num, proc_tree);
        proc_tree->DropQueuedForkChild(child_pid);
      }
      return;
    }
    case FBB_TAG_execv_failed: {
      // FIXME(rbalint) check execv parameter and record what needs to be
      // checked when shortcutting the process
      proc->set_exec_pending(false);
      break;
    }
    case FBB_TAG_exit: {
      const FBB_exit *ic_msg = reinterpret_cast<const FBB_exit *>(fbb_buf);
      proc->exit_result(fbb_exit_get_exit_status(ic_msg),
                        fbb_exit_get_utime_u(ic_msg),
                        fbb_exit_get_stime_u(ic_msg));
      break;
    }
    case FBB_TAG_system: {
      const FBB_system *ic_msg = reinterpret_cast<const FBB_system *>(fbb_buf);
      assert(!proc->system_child());
      // system(cmd) launches a child of argv = ["sh", "-c", cmd]
      auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
      // FIXME what if !has_cmd() ?
      expected_child->set_sh_c_command(fbb_system_get_cmd(ic_msg));
      expected_child->set_launch_type(firebuild::LAUNCH_TYPE_SYSTEM);
      proc->set_expected_child(expected_child);
      break;
    }
    case FBB_TAG_system_ret: {
      assert(proc->system_child());
      if (proc->system_child()->state() != firebuild::FB_PROC_FINALIZED) {
        /* The process has actually quit (otherwise the interceptor
         * couldn't send us the system_ret message), but the supervisor
         * hasn't seen this event yet. Thus we have to slightly defer
         * sending the ACK. */
        proc->system_child()->set_on_finalized_ack(ack_num, fd_conn);
        proc->set_system_child(NULL);
        return;
      }
      /* Else we can ACK straight away. */
      proc->set_system_child(NULL);
      break;
    }
    case FBB_TAG_popen: {
      const FBB_popen *ic_msg = reinterpret_cast<const FBB_popen *>(fbb_buf);
      assert(!proc->pending_popen_child());
      assert(proc->pending_popen_fd() == -1);
      // popen(cmd) launches a child of argv = ["sh", "-c", cmd]
      auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
      // FIXME what if !has_cmd() ?
      expected_child->set_sh_c_command(fbb_popen_get_cmd(ic_msg));
      expected_child->set_launch_type(firebuild::LAUNCH_TYPE_POPEN);
      proc->set_expected_child(expected_child);
      break;
    }
    case FBB_TAG_popen_parent: {
      const FBB_popen_parent *ic_msg = reinterpret_cast<const FBB_popen_parent *>(fbb_buf);
      if (proc->has_expected_child()) {
        /* The child hasn't appeared yet. Defer sending the ACK and setting up
         * the fd -> child mapping. */
        assert(!proc->pending_popen_child());
        proc_tree->QueueParentAck(proc->pid(), ack_num, fd_conn);
        proc->set_pending_popen_fd(fbb_popen_parent_get_fd(ic_msg));
        return;
      }
      /* The child has already appeared. Take a note of the fd -> child mapping. */
      assert(proc->pending_popen_fd() == -1);
      assert(proc->pending_popen_child());
      proc->AddPopenedProcess(fbb_popen_parent_get_fd(ic_msg), proc->pending_popen_child());
      proc->set_pending_popen_child(NULL);
      // FIXME(egmont) Connect pipe's end with child
      break;
    }
    case FBB_TAG_popen_failed: {
      const FBB_popen_failed *ic_msg = reinterpret_cast<const FBB_popen_failed *>(fbb_buf);
      // FIXME what if !has_cmd() ?
      proc->pop_expected_child_fds(
          std::vector<std::string>({"sh", "-c", fbb_popen_failed_get_cmd(ic_msg)}),
          nullptr,
          true);
      break;
    }
    case FBB_TAG_pclose: {
      const FBB_pclose *ic_msg = reinterpret_cast<const FBB_pclose *>(fbb_buf);
      if (!fbb_pclose_has_error_no(ic_msg)) {
        // FIXME(egmont) proc->handle_close(fbb_pclose_get_fd(ic_msg), 0);
        firebuild::ExecedProcess *child = proc->PopPopenedProcess(fbb_pclose_get_fd(ic_msg));
        assert(child);
        if (child->state() != firebuild::FB_PROC_FINALIZED) {
          /* We haven't seen the process quitting yet. Defer sending the ACK. */
          child->set_on_finalized_ack(ack_num, fd_conn);
          return;
        }
        /* Else we can ACK straight away. */
      }
      break;
    }
    case FBB_TAG_posix_spawn: {
      const FBB_posix_spawn *ic_msg = reinterpret_cast<const FBB_posix_spawn *>(fbb_buf);
      auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
      std::vector<std::string> argv = fbb_posix_spawn_get_arg(ic_msg);
      expected_child->set_argv(argv);
      proc->set_expected_child(expected_child);
      proc->set_posix_spawn_pending(true);
      break;
    }
    case FBB_TAG_posix_spawn_parent: {
      const FBB_posix_spawn_parent *ic_msg =
          reinterpret_cast<const FBB_posix_spawn_parent *>(fbb_buf);

      /* First, do the basic fork() */
      auto pid = fbb_posix_spawn_parent_get_pid(ic_msg);
      auto fork_parent_fds = proc->pass_on_fds(false);
      auto fork_child = firebuild::ProcessFactory::getForkedProcess(pid, proc);
      proc_tree->insert(fork_child);
      fork_child->set_state(firebuild::FB_PROC_TERMINATED);

      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Do the corresponding administration. */
      std::vector<std::string> file_actions = fbb_posix_spawn_parent_get_file_actions(ic_msg);
      for (std::string& file_action : file_actions) {
        switch (file_action[0]) {
          case 'o': {
            /* A successful open to a particular fd, silently closing the previous file if any.
             * The string is "o <fd> <flags> <mode> <filename>" (without the angle brackets). */
            int fd, flags, filename_offset;
            sscanf(file_action.c_str(), "o %d %d %*d %n", &fd, &flags, &filename_offset);
            const char *path = file_action.c_str() + filename_offset;
            fork_child->handle_force_close(fd);
            fork_child->handle_open(AT_FDCWD, path, flags, fd, 0);
            break;
          }
          case 'c': {
            /* A close attempt, maybe successful, maybe failed, we don't know. See glibc's
             * sysdeps/unix/sysv/linux/spawni.c:
             *   Signal errors only for file descriptors out of range.
             * sysdeps/posix/spawni.c:
             *   Only signal errors for file descriptors out of range.
             * whereas signaling the error means to abort posix_spawn and thus not reach
             * this code here.
             * The string is "c <fd>" (without the angle brackets). */
            int fd;
            sscanf(file_action.c_str(), "c %d", &fd);
            fork_child->handle_force_close(fd);
            break;
          }
          case 'd': {
            /* A successful dup2.
             * Note that as per https://austingroupbugs.net/view.php?id=411 and glibc's
             * implementation, oldfd==newfd clears the close-on-exec bit (here only,
             * not in a real dup2()).
             * The string is "d <oldfd> <newfd>" (without the angle brackets). */
            int oldfd, newfd;
            sscanf(file_action.c_str(), "d %d %d", &oldfd, &newfd);
            if (oldfd == newfd) {
              fork_child->handle_clear_cloexec(oldfd);
            } else {
              fork_child->handle_dup3(oldfd, newfd, 0, 0);
            }
            break;
          }
          default:
            assert(false);
        }
      }

      proc->set_posix_spawn_pending(false);

      auto posix_spawn_child_sock = proc_tree->Pid2PosixSpawnChildSock(proc->pid());
      if (posix_spawn_child_sock) {
        /* The child has already appeared, but had to wait for this "posix_spawn_parent" message.
         * Let the child continue (respond to the pending "scproc_query" with "scproc_resp"). */
        auto posix_spawn_child = posix_spawn_child_sock->incomplete_child;
        fork_child->set_exec_child(posix_spawn_child);
        posix_spawn_child->set_parent(fork_child);
        posix_spawn_child->set_fds(fork_child->pass_on_fds());
        accept_exec_child(posix_spawn_child, posix_spawn_child_sock->sock, proc_tree);
        proc_tree->DropQueuedPosixSpawnChild(proc->pid());
      } else {
        /* The child hasn't appeared yet. Register a pending exec, just like we do at exec*()
         * calls. This lets us detect a statically linked binary launched by posix_spawn(),
         * exactly the way we do at a regular exec*(), i.e. successfully wait*()ing for a child
         * that is in exec_pending state. */
        std::vector<std::string> arg = fbb_posix_spawn_parent_get_arg(ic_msg);
        proc->pop_expected_child_fds(arg, nullptr);
        fork_child->set_exec_pending(true);
      }
      /* In either case, ACK the "posix_spawn_parent" message, don't necessarily wait for the
       * child to appear. */
      break;
    }
    case FBB_TAG_posix_spawn_failed: {
      const FBB_posix_spawn_failed *ic_msg =
          reinterpret_cast<const FBB_posix_spawn_failed *>(fbb_buf);
      std::vector<std::string> arg = fbb_posix_spawn_failed_get_arg(ic_msg);
      proc->pop_expected_child_fds(arg, nullptr, true);
      proc->set_posix_spawn_pending(false);
      break;
    }
    case FBB_TAG_wait: {
      const FBB_wait *ic_msg = reinterpret_cast<const FBB_wait *>(fbb_buf);
      firebuild::Process *child = proc_tree->pid2proc(fbb_wait_get_pid(ic_msg));
      assert(child);
      if (child->exec_pending()) {
        /* If the supervisor believes an exec is pending in a child proces while the parent
         * actually successfully waited for the child, it means that the child didn't sign in to
         * the supervisor, presumably because it is statically linked. See #324 for details. */
        child->disable_shortcutting_bubble_up(
            "Process did not sign in to supervisor, perhaps statically linked or failed to link");
        /* Need to also clear the exec_pending state for Process::any_child_not_finalized()
         * and finalize this never-seen process. */
        child->set_exec_pending(false);
        child->maybe_finalize();
        /* Ack it straight away. */
      } else if (child->state() != firebuild::FB_PROC_FINALIZED) {
        /* We haven't seen the process quitting yet. Defer sending the ACK. */
        child->set_on_finalized_ack(ack_num, fd_conn);
        return;
      }
      /* Else we can ACK straight away. */
      break;
    }
    case FBB_TAG_execv: {
      const FBB_execv *ic_msg = reinterpret_cast<const FBB_execv *>(fbb_buf);
      proc->update_rusage(fbb_execv_get_utime_u(ic_msg),
                          fbb_execv_get_stime_u(ic_msg));
      // FIXME(rbalint) save execv parameters
      proc->set_exec_pending(true);
      break;
    }
    case FBB_TAG_open: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_open *>(fbb_buf),
                                         fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBB_TAG_dlopen: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dlopen *>(fbb_buf),
                                         fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBB_TAG_close: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_close *>(fbb_buf));
      break;
    }
    case FBB_TAG_unlink: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_unlink *>(fbb_buf));
      break;
    }
    case FBB_TAG_mkdir: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_mkdir *>(fbb_buf));
      break;
    }
    case FBB_TAG_rmdir: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_rmdir *>(fbb_buf));
      break;
    }
    case FBB_TAG_pipe2: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_pipe2 *>(fbb_buf));
      break;
    }
    case FBB_TAG_dup3: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dup3 *>(fbb_buf));
      break;
    }
    case FBB_TAG_dup: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dup *>(fbb_buf));
      break;
    }
    case FBB_TAG_rename: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_rename *>(fbb_buf));
      break;
    }
    case FBB_TAG_symlink: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_symlink *>(fbb_buf));
      break;
    }
    case FBB_TAG_fcntl: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_fcntl *>(fbb_buf));
      break;
    }
    case FBB_TAG_ioctl: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_ioctl *>(fbb_buf));
      break;
    }
    case FBB_TAG_chdir: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_chdir *>(fbb_buf));
      break;
    }
    case FBB_TAG_fchdir: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_fchdir *>(fbb_buf));
      break;
    }
    case FBB_TAG_read: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_read *>(fbb_buf));
      break;
    }
    case FBB_TAG_write: {
      ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_write *>(fbb_buf));
      break;
    }
    case FBB_TAG_access:
    case FBB_TAG_chmod:
    case FBB_TAG_chown:
    case FBB_TAG_euidaccess:
    case FBB_TAG_faccessat:
    case FBB_TAG_fb_debug:
    case FBB_TAG_fb_error:
    case FBB_TAG_fchmod:
    case FBB_TAG_fchown:
    case FBB_TAG_fcloseall:
    case FBB_TAG_fpathconf:
    case FBB_TAG_freopen:
    case FBB_TAG_fstat:
    case FBB_TAG_ftruncate:
    case FBB_TAG_futime:
    case FBB_TAG_getdomainname:
    case FBB_TAG_gethostname:
    case FBB_TAG_link:
    case FBB_TAG_lockf:
    case FBB_TAG_NEXT:
    case FBB_TAG_pathconf:
    case FBB_TAG_readlink:
    case FBB_TAG_scproc_resp:
    case FBB_TAG_stat:
    case FBB_TAG_syscall:
    case FBB_TAG_sysconf:
    case FBB_TAG_testing:
    case FBB_TAG_truncate:
    case FBB_TAG_utime:
      {
      // TODO(rbalint)
      break;
    }
    case FBB_TAG_gen_call: {
      break;
    }
    default: {
      firebuild::fb_error("Unknown FBB message tag: " + std::to_string(tag));
      assert(0 && "Unknown message FBB message tag!");
    }
  }

  if (ack_num != 0) {
    firebuild::ack_msg(fd_conn, ack_num);
  }
}


/**
 * Copy whole file content from in_fd to out_fd retrying on temporary problems.
 * @param out_fd file desctiptor to write content to
 * @param in_fd file desctiptor to read content from
 * @return bytes written, -1 on error
 */
inline ssize_t sendfile_full(int out_fd, int in_fd) {
  char buf[4096];
  ssize_t nread, ret = 0;

  while (nread = read(in_fd, buf, sizeof buf), nread > 0) {
    char *out_ptr = buf;
    ssize_t nwritten;

    do {
      nwritten = write(out_fd, out_ptr, nread);

      if (nwritten >= 0)      {
        nread -= nwritten;
        out_ptr += nwritten;
        ret += nwritten;
      } else if (errno != EINTR)      {
        return -1;
      }
    } while (nread > 0);
  }
  return ret;
}

/**
 * Write report to specified file
 *
 * @param html_filename report file to be written
 * @param datadir report template's location
 * TODO(rbalint) error handling
 */
static void write_report(const std::string &html_filename,
                         const std::string &datadir) {
  const char dot_filename[] = "firebuild-profile.dot";
  const char svg_filename[] = "firebuild-profile.svg";
  // FIXME Use a search path, according to the locations in various popular distributions
  const std::string d3_datadir = "/usr/share/nodejs/d3/dist";
  const char d3_filename[] = "d3.min.js";
  const char tree_filename[] = "firebuild-process-tree.js";
  const char html_orig_filename[] = "build-report.html";
  const std::string dot_cmd = "dot";

  int d3 = open((d3_datadir + "/" + d3_filename).c_str(), O_RDONLY);
  if (d3 == -1) {
    perror("open");
    firebuild::fb_error("Opening file " + (datadir + "/" + d3_filename) +
                        " failed.");
    firebuild::fb_error("Can not write build report.");
    return;
  }

  FILE* src_file = fopen((datadir + "/" + html_orig_filename).c_str(), "r");
  if (src_file == NULL) {
    perror("fopen");
    firebuild::fb_error("Opening file " + (datadir + "/" + html_orig_filename) +
                        " failed.");
    firebuild::fb_error("Can not write build report.");
    return;
  }

  // dirname may modify its parameter thus we provide a writable char string
  char *html_filename_tmp = new char[html_filename.size() + 1];
  strncpy(html_filename_tmp, html_filename.c_str(), html_filename.size() + 1);
  std::string dir = dirname(html_filename_tmp);
  delete[] html_filename_tmp;

  // export profile
  {
    FILE* dot = fopen((dir + "/" + dot_filename).c_str(), "w");
    if (dot == NULL) {
      perror("fopen");
      firebuild::fb_error("Failed to open dot file for writing profile graph.");
    }
    proc_tree->export_profile2dot(dot);
    fclose(dot);
  }

  auto system_cmd =
      dot_cmd + " -Tsvg -o" + dir + "/" + svg_filename + " " + dir + "/" + dot_filename;
  if (system(system_cmd.c_str()) != 0) {
    perror("system");
    firebuild::fb_error("Failed to generate profile graph with the following command: "
                        + system_cmd);
  }

  FILE* dst_file = fopen(html_filename.c_str(), "w");
  int ret = dst_file == NULL ? -1 : 0;
  while ((ret != -1)) {
    char* line = NULL;
    size_t zero = 0;
    if (getline(&line, &zero, src_file) == -1) {
      // finished reading file
      if (!feof(src_file)) {
        perror("getline");
        firebuild::fb_error("Reading from report template failed.");
      }
      free(line);
      break;
    }
    if (strstr(line, d3_filename) != NULL) {
      fprintf(dst_file, "<script type=\"text/javascript\">\n");

      fflush(dst_file);
      ret = sendfile_full(fileno(dst_file), d3);
      fsync(fileno(dst_file));
      fprintf(dst_file, "    </script>\n");
    } else if (strstr(line, tree_filename) != NULL) {
      fprintf(dst_file, "<script type=\"text/javascript\">\n");
      proc_tree->export2js(dst_file);
      fprintf(dst_file, "    </script>\n");
    } else if (strstr(line, svg_filename) != NULL) {
      int svg = open((dir + "/" + svg_filename).c_str(), O_RDONLY);
      fflush(dst_file);
      ret = sendfile_full(fileno(dst_file), svg);
      fsync(fileno(dst_file));
      close(svg);
    } else {
      fprintf(dst_file, "%s", line);
    }
    free(line);
  }
  close(d3);
  fclose(src_file);
  fclose(dst_file);
}

/**
 * Get the system temporary directory to use.
 *
 * TMPDIR is used if it's nonempty.
 * Note that relative path is accepted and used correctly by the
 * firebuild process itself, although the build command it launches
 * might not support it. It's highly recommended to use absolute path.
 *
 * If TMPDIR is unset or empty, use the default "/tmp".
 *
 * @return the system temporary directory to use
 */
static const char *get_tmpdir() {
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir != NULL && tmpdir[0] != '\0') {
    return tmpdir;
  } else {
    return "/tmp";
  }
}

/**
 * Bump RLIMIT_NOFILE to hard limit to allow more parallel interceptor connections.
 */
static void bump_limits() {
  struct rlimit rlim;
  getrlimit(RLIMIT_NOFILE, &rlim);
  /* 8K is expected to be enough for up more than 2K parallel intercepted processes, thus try to
   * bump the limit above that. */
  rlim_t preferred_limit = (rlim.rlim_max == RLIM_INFINITY) ? 8192 : rlim.rlim_max;
  if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < preferred_limit) {
    FB_DEBUG(firebuild::FB_DEBUG_COMM, "Increasing limit of open files from "
             + std::to_string(rlim.rlim_cur) + " to " + std::to_string(preferred_limit) + "");
    rlim.rlim_cur = preferred_limit;
    setrlimit(RLIMIT_NOFILE, &rlim);
  }
}

}  // namespace

/**
 * Create connection sockets for the interceptor
 */
static evutil_socket_t create_listener() {
  evutil_socket_t listener;

  if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_un local;
  local.sun_family = AF_UNIX;
  strncpy(local.sun_path, fb_conn_string, sizeof(local.sun_path) - 1);

  auto len = strlen(local.sun_path) + sizeof(local.sun_family);
  if (bind(listener, (struct sockaddr *)&local, len) == -1) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(listener, 500) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  return listener;
}

static void ic_conn_readcb(evutil_socket_t fd_conn, int16_t what, void *ctx) {
  auto conn_ctx = reinterpret_cast<firebuild::ConnectionContext*>(ctx);
  TRACK(firebuild::FB_DEBUG_COMM, "fd_conn=%d, ctx=%s", fd_conn, D(conn_ctx));

  (void) fd_conn; /* unused in prod build */
  (void) what; /* unused */
  assert(conn_ctx->fd().fd() == fd_conn);  /* makes sure that FD's seq is correct */
  auto proc = conn_ctx->proc;
  auto &buf = conn_ctx->buffer();
  size_t full_length;
  const firebuild::msg_header * header;

  int read_ret = buf.read(conn_ctx->fd(), -1);
  if (read_ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      /* Try again later. */
      return;
    }
  }
  if (read_ret <= 0) {
    FB_DEBUG(firebuild::FB_DEBUG_COMM, "socket " + d(conn_ctx->fd()) +
             " hung up (" + d(proc) + ")");
    delete conn_ctx;
    return;
  }

  do {
    if (buf.length() < sizeof(*header)) {
      /* Header is still incomplete, try again later. */
      return;
    } else {
      header = reinterpret_cast<const firebuild::msg_header*>(buf.data());
      full_length = sizeof(*header) + header->msg_size;
      if (buf.length() < full_length) {
        /* Have partial message, more data is needed. */
        return;
      }
    }

    /* Have at least one full message. */
    auto fbb_msg = buf.data() + sizeof(*header);

    if (FB_DEBUGGING(firebuild::FB_DEBUG_COMM)) {
      FB_DEBUG(firebuild::FB_DEBUG_COMM, "fd " + d(conn_ctx->fd()) + ": (" + d(proc) + ")");
      if (header->ack_id) {
        fprintf(stderr, "ack_num: %d\n", header->ack_id);
      }
      fbb_debug(fbb_msg);
      fflush(stderr);
    }

    /* Process the messaage. */
    if (proc) {
      proc_ic_msg(fbb_msg, header->ack_id, conn_ctx->fd(), proc);
    } else {
      /* Fist interceptor message */
      proc_new_process_msg(fbb_msg, header->ack_id, conn_ctx->fd(), &conn_ctx->proc);
    }
    buf.discard(full_length);
  } while (buf.length() > 0);
}


static void save_child_status(pid_t pid, int status, int * ret, bool runaway) {
  TRACK(firebuild::FB_DEBUG_PROC, "pid=%d, status=%d, runaway=%s", pid, status, D(runaway));

  if (WIFEXITED(status)) {
    *ret = WEXITSTATUS(status);
    FB_DEBUG(firebuild::FB_DEBUG_COMM, std::string(runaway ? "runaway" : "child")
             + " process exited with status " + std::to_string(*ret) + ". ("
             + d(proc_tree->pid2proc(pid)) + ")");
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr, "%s process has been killed by signal %d",
            runaway ? "Runaway" : "Child",
            WTERMSIG(status));
  }
}


/** Stop listener on SIGCHLD */
static void sigchild_cb(evutil_socket_t fd, int16_t what, void *arg) {
  TRACK(firebuild::FB_DEBUG_PROC, "");

  auto listener_event = reinterpret_cast<struct event *>(arg);
  (void)fd;
  (void)what;

  int status = 0;
  pid_t waitpid_ret;

  /* Collect exiting children. */
  do {
    waitpid_ret = waitpid(-1, &status, WNOHANG);
    if (waitpid_ret == child_pid) {
      save_child_status(waitpid_ret, status, &child_ret, false);
    } else if (waitpid_ret > 0) {
      // TODO(rbalint) find runaway child's parent and possibly disable shortcutting
      int ret = -1;
      save_child_status(waitpid_ret, status, &ret, true);
    }
  } while (waitpid_ret > 0);
  if (waitpid_ret < 0) {
    /* All children exited. */
    event_del(sigchild_event);
    event_del(listener_event);
  }
}


static void accept_ic_conn(evutil_socket_t listener, int16_t event, void *arg) {
  TRACK(firebuild::FB_DEBUG_COMM, "listener=%d", listener);

  struct sockaddr_storage remote;
  socklen_t slen = sizeof(remote);
  (void) event; /* unused */
  (void) arg;   /* unused */

  int fd = accept(listener, (struct sockaddr*)&remote, &slen);
  if (fd < 0) {
    perror("accept");
  } else {
    auto conn_ctx = new firebuild::ConnectionContext(proc_tree, fd);
    evutil_make_socket_nonblocking(fd);
    auto ev = event_new(ev_base, fd, EV_READ | EV_PERSIST, ic_conn_readcb, conn_ctx);
    conn_ctx->set_ev(ev);
    event_add(ev, NULL);
  }
}

int main(const int argc, char *argv[]) {
  char *config_file = NULL;
  char *directory = NULL;
  std::list<std::string> config_strings = {};
  int c;

  // running under BATS fd 3 is inherited
  if (fcntl(3, F_GETFD) != -1 || errno != EBADF) {
    inherited_fd = 3;
  }

  // init global data
  cfg = new libconfig::Config();
  proc_tree = new firebuild::ProcessTree();


  // parse options
  setenv("POSIXLY_CORRECT", "1", true);
  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      {"config-file",          required_argument, 0, 'c' },
      {"directory",            required_argument, 0, 'C' },
      {"debug-level",          required_argument, 0, 'd' },
      {"generate-report",      optional_argument, 0, 'r' },
      {"help",                 no_argument,       0, 'h' },
      {"option",               required_argument, 0, 'o' },
      {"insert-trace-markers", no_argument,       0, 'i' },
      {0,                                0,       0,  0  }
    };

    c = getopt_long(argc, argv, "c:C:d:r::o:hi",
                    long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'c':
      config_file = optarg;
      break;

    case 'C':
      directory = optarg;
      break;

    case 'd':
      firebuild::debug_flags = firebuild::parse_debug_flags(optarg);
      break;

    case 'h':
      usage();
      exit(EXIT_SUCCESS);
      // break;

    case 'o':
      if (optarg != NULL) {
        config_strings.push_back(std::string(optarg));
      } else {
        usage();
        exit(EXIT_FAILURE);
      }
      break;

    case 'i':
      insert_trace_markers = true;
      break;

    case 'r':
      generate_report = true;
      if (optarg != NULL) {
        report_file = optarg;
      }
      break;

    default:
      usage();
      exit(EXIT_FAILURE);
    }
  }
  if (optind >= argc) {
    usage();
    exit(EXIT_FAILURE);
  }

  firebuild::read_config(cfg, config_file, config_strings);

  // Initialize the cache
  std::string cache_dir;
  const char *cache_dir_env = getenv("FIREBUILD_CACHE_DIR");
  if (cache_dir_env == NULL || cache_dir_env[0] == '\0') {
    const char *home_env = getenv("HOME");
    cache_dir = std::string(home_env) + "/.fbcache";
  } else {
    cache_dir = std::string(cache_dir_env);
  }

  struct stat st;
  if (stat(cache_dir.c_str(), &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      firebuild::fb_error("cache dir exists but is not a directory");
      exit(EXIT_FAILURE);
    }
  } else {
    if (mkdir(cache_dir.c_str(), 0700) != 0) {
      perror("mkdir");
      exit(EXIT_FAILURE);
    }
  }
  firebuild::blob_cache = new firebuild::BlobCache(cache_dir + "/blobs");
  firebuild::obj_cache = new firebuild::ObjCache(cache_dir + "/objs");
  /* Like CCACHE_READONLY: Don't store new results in the cache. */
  bool no_store = (getenv("FIREBUILD_READONLY") != NULL);
  /* Like CCACHE_RECACHE: Don't fetch entries from the cache, but still
   * potentially store new ones. Note however that it might decrease the
   * objcache hit ratio: new entries might be stored that eventually
   * result in the same operation, but go through a slightly different
   * path (e.g. different tmp file name), and thus look different in
   * Firebuild's eyes. Firebuild refuses to shortcut a process if two or
   * more matches are found in the objcache. */
  bool no_fetch = (getenv("FIREBUILD_RECACHE") != NULL);
  cacher =
      new firebuild::ExecedProcessCacher(no_store, no_fetch,
                                         cfg->getRoot()["env_vars"]["fingerprint_skip"]);
  firebuild::hash_cache = new firebuild::HashCache();

  {
    char *pattern;
    if (asprintf(&pattern, "%s/firebuild.XXXXXX", get_tmpdir()) < 0) {
      perror("asprintf");
    }
    fb_tmp_dir = mkdtemp(pattern);
    if (fb_tmp_dir == NULL) {
      perror("mkdtemp");
      exit(EXIT_FAILURE);
    }
    fb_conn_string = strdup((std::string(fb_tmp_dir) + "/socket").c_str());
  }
  auto env_exec = get_sanitized_env();

  ev_base = event_base_new();;

  /* Open listener socket before forking child to always let the child connect */
  listener = create_listener();
  auto listener_event = event_new(ev_base, listener, EV_READ|EV_PERSIST, accept_ic_conn, NULL);
  event_add(listener_event, NULL);
  sigchild_event = event_new(ev_base, SIGCHLD, EV_SIGNAL|EV_PERSIST, sigchild_cb, listener_event);
  event_add(sigchild_event, NULL);

  /* Collect runaway children */
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  // run command and handle interceptor messages
  if ((child_pid = fork()) == 0) {
    int i;
    // intercepted process
    char* argv_exec[argc - optind + 1];

    // we don't need that
    evutil_closesocket(listener);
    // create and execute build command
    for (i = 0; i < argc - optind ; i++) {
      argv_exec[i] = argv[optind + i];
    }
    argv_exec[i] = NULL;

    if (directory != NULL && chdir(directory) != 0) {
      perror("chdir");
      exit(EXIT_FAILURE);
    }

    execvpe(argv[optind], argv_exec, env_exec);
    perror("Executing build command failed");
    exit(EXIT_FAILURE);
  } else {
    // supervisor process

    bump_limits();
    /* no SIGPIPE if a supervised process we're writing to unexpectedly dies */
    signal(SIGPIPE, SIG_IGN);

    /* Main loop for processing interceptor messages */
    event_base_dispatch(ev_base);

    /* Clean up remaining events and the event base. */
    evutil_closesocket(listener);
    event_free(listener_event);
    event_free(sigchild_event);
    event_base_free(ev_base);
  }

  if (!proc_tree->root()) {
    fprintf(stderr, "ERROR: Could not collect any information about the build "
            "process\n");
    child_ret = EXIT_FAILURE;
  } else {
    // postprocess process tree
    proc_tree->root()->sum_rusage_recurse();

    // show process tree if needed
    if (generate_report) {
      write_report(report_file, datadir);
    }
  }

  // clean up everything
  {
    char* env_str;
    for (int i = 0; (env_str = env_exec[i]) != NULL; i++) {
      free(env_str);
    }
    free(env_exec);
  }

  unlink(fb_conn_string);
  rmdir(fb_tmp_dir);

  free(fb_conn_string);
  free(fb_tmp_dir);
  delete(proc_tree);
  delete(firebuild::ignore_locations);
  delete(cfg);

  // keep Valgrind happy
  fclose(stdin);
  fclose(stdout);
  fclose(stderr);
  if (inherited_fd > -1) {
    close(inherited_fd);
  }

  exit(child_ret);
}

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}


namespace firebuild {

/** Print error message */
extern void fb_error(const std::string &msg) {
  std::cerr << "FireBuild error: " << msg << std::endl;
}

/** Print debug message if debug level is at least lvl */
extern void fb_debug(const std::string &msg) {
  std::cerr << msg << std::endl;
}

int32_t debug_flags = 0;

}  // namespace firebuild
