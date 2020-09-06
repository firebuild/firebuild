/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
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
#include "firebuild/cache.h"
#include "firebuild/multi_cache.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/process_factory.h"
#include "firebuild/process_tree.h"
#include "firebuild/process_proto_adaptor.h"
#include "firebuild/utils.h"
#include "firebuild/fb-cache.pb.h"
#include "./fbb.h"

/** global configuration */
libconfig::Config * cfg;
bool generate_report = false;

namespace {

static char datadir[] = FIREBUILD_DATADIR;

static char *fb_tmp_dir;
/** The connection sockets are derived from the fb_conn_string by appending an integer. */
static char *fb_conn_string;

/** Pool of listenter sockets
 *
 * The interceptor creates parallel connections from each intercepted process
 * and the parallel connections require separate sockets.
 *
 * Each process need one socket for the supervisor connection,
 * 2 for stdout and stderr, 2 for a potential pipe setting up stdio for the next
 * exec()-ed process.
 *
 * When the interceptor creates more pipes it can't connect them to the
 * supervisor and children of the process may not be shortcuttable.
 */
static int fb_listener_pool[5];

static int sigchld_fds[2];
static FILE * sigchld_stream;

static int inherited_fd = -1;
static int child_pid, child_ret = 1;
static google::protobuf::io::FileOutputStream * error_fos;
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

/**
 * signal handler for SIGCHLD
 *
 * It send a 0 to the special file descriptor select is listening on, too.
 */
static void sigchld_handler(const int /*sig */) {
  char buf[] = {0};
  int status = 0;

  waitpid(child_pid, &status, WNOHANG);
  if (WIFEXITED(status)) {
    child_ret = WEXITSTATUS(status);
    write(sigchld_fds[1], buf, sizeof(buf));
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr, "Child process has been killed by signal %d",
            WTERMSIG(status));
    write(sigchld_fds[1], buf, 1);
  }
}

/**
 * Initialize signal handlers
 */
static void init_signal_handlers(void) {
  struct sigaction sa;

  sa.sa_handler = sigchld_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  /* prepare sigchld_fd */

  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("Could not set up signal handler for SIGCHLD.");
    exit(EXIT_FAILURE);
  }
  if (pipe(sigchld_fds) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  if ((sigchld_stream = fdopen(sigchld_fds[1], "w")) == NULL) {
    perror("fdopen");
    exit(EXIT_FAILURE);
  }

  if (setvbuf(sigchld_stream, NULL, _IONBF, 0) != 0) {
    perror("setvbuf");
    exit(EXIT_FAILURE);
  }
}

/*
 * Check if either exe or arg0 matches any of the entries in the list.
 */
static bool exe_matches_list(const std::string& exe,
                             const std::string& arg0,
                             const libconfig::Setting& list) {
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

static void accept_exec_child(firebuild::ExecedProcess* proc, int fd_conn,
                              firebuild::ProcessTree* proc_tree) {
    FBB_Builder_scproc_resp sv_msg;
    fbb_scproc_resp_init(&sv_msg);

    proc_tree->insert(proc, fd_conn);
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
      if (firebuild::debug_flags != 0) {
        fbb_scproc_resp_set_debug_flags(&sv_msg, firebuild::debug_flags);
      }
    }

    fbb_send(fd_conn, &sv_msg, 0);
}

/**
 * Process message coming from interceptor
 * @param fb_conn file desctiptor of the connection
 */
void proc_ic_msg(uint32_t ack_num,
                 const void *fbb_buf,
                 int fd_conn) {
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
      assert(parent->state() != firebuild::FB_PROC_FINALIZED);
      if (parent->state() == firebuild::FB_PROC_TERMINATED) {
        fds = parent->pass_on_fds();
      } else {
        /* Queue the ExecedProcess until parent's connection is closed */
        auto proc =
            firebuild::ProcessFactory::getExecedProcess(
                ic_msg, parent, fds);
        proc_tree->QueueExecChild(parent->pid(), fd_conn, proc);
        return;
      }
    } else if (!parent && ppid != getpid()) {
      /* Locate the parent in case of system/popen/posix_spawn, but not
       * when the first intercepter process starts up. */
      unix_parent = proc_tree->pid2proc(ppid);
      assert(unix_parent != NULL);

      /* Verify that the child was expected and get inherited fds. */
      std::shared_ptr<std::vector<std::string>> file_actions;
      std::vector<std::string> args = fbb_scproc_query_get_arg(ic_msg);
      fds = unix_parent->pop_expected_child_fds(args, &file_actions, &launch_type);

      /* Add a ForkedProcess for the forked child we never directly saw. */
      parent = new firebuild::ForkedProcess(pid, ppid, unix_parent, fds);

      /* The actual forked process might have already performed some file operations
       * according to posix_spawn()'s file_actions. Do the corresponding administration
       * in the ForkedProcess. */
      if (file_actions) {
        for (std::string& file_action : *file_actions) {
          switch (file_action[0]) {
            case 'o': {
              /* A successful open to a particular fd, silently closing the previous file if any.
               * The string is "o <fd> <flags> <mode> <filename>" (without the angle brackets). */
              int fd, flags, filename_offset;
              sscanf(file_action.c_str(), "o %d %d %*d %n", &fd, &flags, &filename_offset);
              const char *path = file_action.c_str() + filename_offset;
              parent->handle_force_close(fd);
              parent->handle_open(path, flags, fd, 0);
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
              parent->handle_force_close(fd);
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
                parent->handle_clear_cloexec(oldfd);
              } else {
                parent->handle_dup3(oldfd, newfd, 0, 0);
              }
              break;
            }
            default:
              assert(false);
          }
        }
      }

      /* For the intermediate ForkedProcess where posix_spawn()'s file_actions were executed,
       * we still had all the fds, even the close-on-exec ones. Now it's time to close them. */
      fds = parent->pass_on_fds();

      parent->set_state(firebuild::FB_PROC_TERMINATED);
      // FIXME set exec_child_ ???
      proc_tree->insert(parent, -1);

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

  } else if (tag == FBB_TAG_fork_child) {
    const FBB_fork_child *ic_msg = reinterpret_cast<const FBB_fork_child *>(fbb_buf);
    auto pid = fbb_fork_child_get_pid(ic_msg);
    auto ppid = fbb_fork_child_get_ppid(ic_msg);
    auto fork_parent_fds = proc_tree->Pid2ForkParentFds(pid);
    /* The supervisor needs up to date information about the fork parent in the ProcessTree
     * when the child Process is created. To ensure having up to date information all the
     * messages must be processed from the fork parent up to ForkParent and only then can
     * the child Process created in the ProcessTree and let the child process continue execution.
     */
    if (!fork_parent_fds) {
      /* queue fork_child data and delay processing messages on this socket */
      proc_tree->QueueForkChild(pid, fd_conn, ppid, ack_num);
    } else {
      /* record new process */
      auto pproc = proc_tree->pid2proc(ppid);
      auto proc =
          firebuild::ProcessFactory::getForkedProcess(pid, pproc, fork_parent_fds);
      proc_tree->insert(proc, fd_conn);
      proc_tree->DropForkParentFds(pid);
      firebuild::ack_msg(fd_conn, ack_num);
    }
    return;
  } else if (tag == FBB_TAG_fork_parent) {
    const FBB_fork_parent *ic_msg = reinterpret_cast<const FBB_fork_parent *>(fbb_buf);
    auto child_pid = fbb_fork_parent_get_pid(ic_msg);
    /* here pproc is the current process as it is the fork parent */
    auto pproc = proc_tree->Sock2Proc(fd_conn);
    auto fork_child_sock = proc_tree->Pid2ForkChildSock(child_pid);
    if (!fork_child_sock) {
      /* queue fork_parent data */
      proc_tree->SaveForkParentState(child_pid, pproc->pass_on_fds(false));
    } else {
      /* record new child process */
      auto proc =
          firebuild::ProcessFactory::getForkedProcess(child_pid, pproc,
                                                      pproc->pass_on_fds(false));
      proc_tree->insert(proc, fork_child_sock->sock);
      firebuild::ack_msg(fork_child_sock->sock, fork_child_sock->ack_num);
      proc_tree->DropQueuedForkChild(child_pid);
    }
  } else if (tag == FBB_TAG_execv_failed) {
    auto *proc = proc_tree->Sock2Proc(fd_conn);
    // FIXME(rbalint) check execv parameter and record what needs to be
    // checked when shortcutting the process
    proc->set_exec_pending(false);
  } else if (tag == FBB_TAG_exit ||
             tag == FBB_TAG_execv ||
             tag == FBB_TAG_system ||
             tag == FBB_TAG_system_ret ||
             tag == FBB_TAG_popen ||
             tag == FBB_TAG_popen_parent ||
             tag == FBB_TAG_popen_failed ||
             tag == FBB_TAG_pclose ||
             tag == FBB_TAG_posix_spawn ||
             tag == FBB_TAG_posix_spawn_parent ||
             tag == FBB_TAG_posix_spawn_failed ||
             tag == FBB_TAG_wait ||
             tag == FBB_TAG_open ||
             tag == FBB_TAG_dlopen ||
             tag == FBB_TAG_close ||
             tag == FBB_TAG_pipe2 ||
             tag == FBB_TAG_dup3 ||
             tag == FBB_TAG_dup ||
             tag == FBB_TAG_rename ||
             tag == FBB_TAG_fcntl ||
             tag == FBB_TAG_ioctl ||
             tag == FBB_TAG_chdir ||
             tag == FBB_TAG_read ||
             tag == FBB_TAG_write) {
    try {
      ::firebuild::Process *proc = proc_tree->Sock2Proc(fd_conn);
      if (tag == FBB_TAG_exit) {
        const FBB_exit *ic_msg = reinterpret_cast<const FBB_exit *>(fbb_buf);
        proc->exit_result(fbb_exit_get_exit_status(ic_msg),
                          fbb_exit_get_utime_u(ic_msg),
                          fbb_exit_get_stime_u(ic_msg));
      } else if (tag == FBB_TAG_system) {
        const FBB_system *ic_msg = reinterpret_cast<const FBB_system *>(fbb_buf);
        assert(!proc->system_child());
        // system(cmd) launches a child of argv = ["sh", "-c", cmd]
        auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
        // FIXME what if !has_cmd() ?
        expected_child->set_sh_c_command(fbb_system_get_cmd(ic_msg));
        expected_child->set_launch_type(firebuild::LAUNCH_TYPE_SYSTEM);
        proc->set_expected_child(expected_child);
      } else if (tag == FBB_TAG_system_ret) {
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
      } else if (tag == FBB_TAG_popen) {
        const FBB_popen *ic_msg = reinterpret_cast<const FBB_popen *>(fbb_buf);
        assert(!proc->pending_popen_child());
        assert(proc->pending_popen_fd() == -1);
        // popen(cmd) launches a child of argv = ["sh", "-c", cmd]
        auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
        // FIXME what if !has_cmd() ?
        expected_child->set_sh_c_command(fbb_popen_get_cmd(ic_msg));
        expected_child->set_launch_type(firebuild::LAUNCH_TYPE_POPEN);
        proc->set_expected_child(expected_child);
      } else if (tag == FBB_TAG_popen_parent) {
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
      } else if (tag == FBB_TAG_popen_failed) {
        const FBB_popen_failed *ic_msg = reinterpret_cast<const FBB_popen_failed *>(fbb_buf);
        // FIXME what if !has_cmd() ?
        proc->pop_expected_child_fds(
            std::vector<std::string>({"sh", "-c", fbb_popen_failed_get_cmd(ic_msg)}),
            nullptr,
            nullptr,
            true);
      } else if (tag == FBB_TAG_pclose) {
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
      } else if (tag == FBB_TAG_posix_spawn) {
        const FBB_posix_spawn *ic_msg = reinterpret_cast<const FBB_posix_spawn *>(fbb_buf);
        std::vector<std::string> file_actions = fbb_posix_spawn_get_file_actions(ic_msg);
        auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false),
                                                                file_actions);
        std::vector<std::string> argv = fbb_posix_spawn_get_arg(ic_msg);
        expected_child->set_argv(argv);
        proc->set_expected_child(expected_child);
      } else if (tag == FBB_TAG_posix_spawn_parent) {
        if (proc->has_expected_child()) {
          /* skip ACK and send it when the the child arrives */
          proc_tree->QueueParentAck(proc->pid(), ack_num, fd_conn);
          return;
        }
      } else if (tag == FBB_TAG_posix_spawn_failed) {
        const FBB_posix_spawn_failed *ic_msg =
            reinterpret_cast<const FBB_posix_spawn_failed *>(fbb_buf);
        std::vector<std::string> arg = fbb_posix_spawn_failed_get_arg(ic_msg);
        proc->pop_expected_child_fds(arg, nullptr, nullptr, true);
      } else if (tag == FBB_TAG_wait) {
        const FBB_wait *ic_msg = reinterpret_cast<const FBB_wait *>(fbb_buf);
        firebuild::Process *child = proc_tree->pid2proc(fbb_wait_get_pid(ic_msg));
        assert(child);
        if (child->state() != firebuild::FB_PROC_FINALIZED) {
          /* We haven't seen the process quitting yet. Defer sending the ACK. */
          child->set_on_finalized_ack(ack_num, fd_conn);
          return;
        }
        /* Else we can ACK straight away. */
      } else if (tag == FBB_TAG_execv) {
        const FBB_execv *ic_msg = reinterpret_cast<const FBB_execv *>(fbb_buf);
        proc->update_rusage(fbb_execv_get_utime_u(ic_msg),
                            fbb_execv_get_stime_u(ic_msg));
        // FIXME(rbalint) save execv parameters
        proc->set_exec_pending(true);
      } else if (tag == FBB_TAG_open) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_open *>(fbb_buf));
      } else if (tag == FBB_TAG_dlopen) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dlopen *>(fbb_buf));
      } else if (tag == FBB_TAG_close) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_close *>(fbb_buf));
      } else if (tag == FBB_TAG_pipe2) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_pipe2 *>(fbb_buf));
      } else if (tag == FBB_TAG_dup3) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dup3 *>(fbb_buf));
      } else if (tag == FBB_TAG_dup) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_dup *>(fbb_buf));
      } else if (tag == FBB_TAG_rename) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_rename *>(fbb_buf));
      } else if (tag == FBB_TAG_fcntl) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_fcntl *>(fbb_buf));
      } else if (tag == FBB_TAG_ioctl) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_ioctl *>(fbb_buf));
      } else if (tag == FBB_TAG_chdir) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_chdir *>(fbb_buf));
      } else if (tag == FBB_TAG_read) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_read *>(fbb_buf));
      } else if (tag == FBB_TAG_write) {
        ::firebuild::ProcessPBAdaptor::msg(proc, reinterpret_cast<const FBB_write *>(fbb_buf));
      }
    } catch (std::out_of_range&) {
      FB_DEBUG(firebuild::FB_DEBUG_COMM, "Ignoring message on fd: " + std::to_string(fd_conn) +
                              std::string(", process probably exited already."));
    }
  } else if (tag == FBB_TAG_gen_call) {
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
  const char d3_filename[] = "d3.v5.min.js";
  const char tree_filename[] = "firebuild-process-tree.js";
  const char html_orig_filename[] = "build-report.html";
  const std::string dot_cmd = "dot";

  int d3 = open((datadir + "/" + d3_filename).c_str(), O_RDONLY);
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

  system((dot_cmd + " -Tsvg -o" + dir + "/" + svg_filename + " " + dir
          + "/" + dot_filename).c_str());

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

}  // namespace


/**
 * Create connection sockets for the interceptor
 */
static void init_listeners() {
  int i = 0;
  for (auto &listener : fb_listener_pool) {
    if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      perror("socket");
      exit(EXIT_FAILURE);
    }

    struct sockaddr_un local;
    local.sun_family = AF_UNIX;
    snprintf(local.sun_path, sizeof(local.sun_path), "%s%d", fb_conn_string, i);

    auto len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(listener, (struct sockaddr *)&local, len) == -1) {
      perror("bind");
      exit(EXIT_FAILURE);
    }

    if (listen(listener, 500) == -1) {
      perror("listen");
      exit(EXIT_FAILURE);
    }
    i++;
  }
}

/**
 * Close all listeners
 */
static void close_listeners() {
  for (auto const &listener : fb_listener_pool) {
    close(listener);
  }
}

/**
 * Is the fd a listener
 *
 * @param fd The fd to check
 * @return true if the fd is a listener
 */
static bool is_listener(int const fd) {
  for (auto const &listener : fb_listener_pool) {
    if (fd == listener) {
      return true;
    }
  }
  return false;
}

int main(const int argc, char *argv[]) {
  char *config_file = NULL;
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
      {"debug-level",          required_argument, 0, 'd' },
      {"generate-report",      optional_argument, 0, 'r' },
      {"help",                 no_argument,       0, 'h' },
      {"option",               required_argument, 0, 'o' },
      {"insert-trace-markers", no_argument,       0, 'i' },
      {0,                                0,       0,  0  }
    };

    c = getopt_long(argc, argv, "c:d:r::o:hi",
                    long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'c':
      config_file = optarg;
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
  auto cache = new firebuild::Cache(cache_dir + "/blobs");
  auto multi_cache = new firebuild::MultiCache(cache_dir + "/pbs");
  /* Like CCACHE_READONLY: Don't store new results in the cache. */
  bool no_store = (getenv("FIREBUILD_READONLY") != NULL);
  /* Like CCACHE_RECACHE: Don't fetch entries from the cache, but still
   * potentially store new ones. Note however that it might decrease the
   * multicache hit ratio: new entries might be stored that eventually
   * result in the same operation, but go through a slightly different
   * path (e.g. different tmp file name), and thus look different in
   * Firebuild's eyes. Firebuild refuses to shortcut a process if two or
   * more matches are found in the protobuf multicache. */
  bool no_fetch = (getenv("FIREBUILD_RECACHE") != NULL);
  cacher =
      new firebuild::ExecedProcessCacher(cache, multi_cache, no_store, no_fetch,
                                         cfg->getRoot()["env_vars"]["fingerprint_skip"]);

  // Verify that the version of the ProtoBuf library that we linked against is
  // compatible with the version of the headers we compiled against.
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  error_fos = new google::protobuf::io::FileOutputStream(STDERR_FILENO);
  {
    char *pattern;
    asprintf(&pattern, "%s/firebuild.XXXXXX", get_tmpdir());
    fb_tmp_dir = mkdtemp(pattern);
    if (fb_tmp_dir == NULL) {
      perror("mkdtemp");
      exit(EXIT_FAILURE);
    }
    fb_conn_string = strdup((std::string(fb_tmp_dir) + "/socket.").c_str());
  }
  auto env_exec = get_sanitized_env();

  init_signal_handlers();

  init_listeners();

  // run command and handle interceptor messages
  if ((child_pid = fork()) == 0) {
    int i;
    // intercepted process
    char* argv_exec[argc - optind + 1];

    // we don't need those
    close(sigchld_fds[0]);
    close(sigchld_fds[1]);
    close_listeners();
    // create and execute build command
    for (i = 0; i < argc - optind ; i++) {
      argv_exec[i] = argv[optind + i];
    }
    argv_exec[i] = NULL;

    execvpe(argv[optind], argv_exec, env_exec);
    perror("Executing build command failed");
    exit(EXIT_FAILURE);
  } else {
    // supervisor process
    int fdmax = sigchld_fds[0];  // maximum file descriptor number

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()

    bool child_exited = false;

    uid_t euid = geteuid();

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    /* no SIGPIPE if a supervised process we're writing to unexpectedly dies */
    signal(SIGPIPE, SIG_IGN);

    // add the listener and and fd listening for child's death to the
    // master set
    for (auto const &listener : fb_listener_pool) {
      FD_SET(listener, &master);
      fdmax = listener > fdmax ? listener : fdmax;
    }
    FD_SET(sigchld_fds[0], &master);

    // main loop for processing interceptor messages
    for (;;) {
      int i;
      if (child_exited) {
        break;
      }
      read_fds = master;  // copy it
      if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
        if (errno != EINTR) {
          perror("select");
          exit(1);
        } else {
          break;
        }
      }

      for (i = 0; i <= fdmax; i++) {
        if (FD_ISSET(i, &read_fds)) {  // we got one!!
          // fd_handled = true;
          if (is_listener(i)) {
            // handle new connections
            struct sockaddr_un remote;
            socklen_t addrlen = sizeof(remote);

            // newly accept()ed socket descriptor
            int newfd = accept(i,
                               (struct sockaddr *)&remote,
                               &addrlen);
            if (newfd == -1) {
              perror("accept");
            } else {
              struct ucred creds;
              socklen_t optlen = sizeof(creds);
              getsockopt(newfd, SOL_SOCKET, SO_PEERCRED, &creds, &optlen);
              if (euid != creds.uid) {
                // someone else started using the socket
                fprintf(stderr,
                        "Unauthorized connection from pid %d, uid %d, gid %d"
                        "\n",
                        creds.pid, creds.uid, creds.gid);
                close(newfd);
              } else {
                FD_SET(newfd, &master);  // add to master set
                if (newfd > fdmax) {    // keep track of the max
                  fdmax = newfd;
                }
              }
              // TODO(rbalint) debug
            }
          } else if (i == sigchld_fds[0]) {
            // Our child has exited.
            // Process remaining messages, then we are done.
            child_exited = true;
            continue;
          } else {
            // handle data from a client
            char *buf;
            ssize_t nbytes;
            uint32_t ack_num;

            if ((nbytes = firebuild::fb_recv_msg(&buf, &ack_num, i)) <= 0) {
              // got error or connection closed by client
              if (nbytes == 0) {
                // connection closed
                FB_DEBUG(firebuild::FB_DEBUG_COMM, "socket " + std::to_string(i) +
                                        std::string(" hung up"));
              } else {
                perror("recv");
              }
              auto proc = proc_tree->Sock2Proc(i);
              if (proc) {
                auto exec_child_sock = proc_tree->Pid2ExecChildSock(proc->pid());
                if (exec_child_sock) {
                  auto exec_child = exec_child_sock->incomplete_child;
                  exec_child->set_fds(proc->pass_on_fds());
                  accept_exec_child(exec_child, exec_child_sock->sock, proc_tree);
                  proc_tree->DropQueuedExecChild(proc->pid());
                }
              }
              proc_tree->finished(i);
              close(i);  // bye!
              FD_CLR(i, &master);  // remove from master set
            } else {
              assert(nbytes >= (ssize_t) sizeof(int));
              if (FB_DEBUGGING(firebuild::FB_DEBUG_COMM)) {
                FB_DEBUG(firebuild::FB_DEBUG_COMM, "fd " + std::to_string(i) + std::string(": "));
                if (ack_num) {
                  fprintf(stderr, "ack_num: %d\n", ack_num);
                }
                fbb_debug(buf);
                fflush(stderr);
              }
              proc_ic_msg(ack_num, buf, i);
              delete[] buf;
            }
          }
        }
      }
    }
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

  close_listeners();
  for (size_t i = 0; i < (sizeof(fb_listener_pool) / sizeof(fb_listener_pool[0])); i++) {
    close(fb_listener_pool[i]);
    unlink((std::string(fb_conn_string) + std::to_string(i)).c_str());
  }
  rmdir(fb_tmp_dir);

  delete(error_fos);
  fclose(sigchld_stream);
  close(sigchld_fds[0]);
  free(fb_conn_string);
  free(fb_tmp_dir);
  delete(proc_tree);
  delete(cfg);

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();

  // keep Valgrind happy
  fclose(stdin);
  fclose(stdout);
  fclose(stderr);
  if (inherited_fd > -1) {
    close(inherited_fd);
  }

  exit(child_ret);
}

/** wrapper for read() retrying on recoverable errors */
ssize_t fb_read(int fd, void *buf, size_t count) {
  FB_READ_WRITE(read, fd, buf, count);
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
