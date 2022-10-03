/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include <signal.h>
#include <getopt.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>
#include <libgen.h>

#include <map>
#include <string>
#include <stdexcept>
#include <libconfig.h++>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/config.h"
#include "firebuild/blob_cache.h"
#include "firebuild/connection_context.h"
#include "firebuild/epoll.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/obj_cache.h"
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

/** global configuration */
libconfig::Config * cfg;
bool generate_report = false;

firebuild::Epoll *epoll = nullptr;

int sigchild_selfpipe[2];

firebuild::ProcessTree *proc_tree;

namespace {

static char *fb_tmp_dir;
static char *fb_conn_string;

int listener;

static int bats_inherited_fd = -1;
static int child_pid, child_ret = 1;

#ifdef FB_EXTRA_DEBUG
static bool insert_trace_markers = false;
#endif
static const char *report_file = "firebuild-build-report.html";
static firebuild::ExecedProcessCacher *cacher;

/** only if debugging "time" */
struct timespec start_time;

static void usage() {
  printf("Usage: firebuild [OPTIONS] <BUILD COMMAND>\n"
         "Execute BUILD COMMAND with Firebuild instrumentation\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "   -c --config-file=FILE     use FILE as configuration file\n"
         "   -C --directory=DIR        change directory before running the command\n"
         "   -d --debug-flags=list     comma separated list of debug flags,\n"
         "                             \"-d help\" to get a list.\n"
         "   -D --debug-filter=list    comma separated list of commands to debug.\n"
         "                             Debug messages related to processes which are not listed\n"
         "                             are suppressed.\n"
         "   -r --generate-report[=HTML] generate a report on the build command execution.\n"
         "                             the report's filename can be specified \n"
         "                             (firebuild-build-report.html by default). \n"
         "   -h --help                 show this help\n"
         "   -o --option=key=val       Add or replace a scalar in the config\n"
         "   -o --option=key+=val      Append to an array of scalars in the config\n"
         "   -o --option=key-=val      Remove from an array of scalars in the config\n"
         "   -i --insert-trace-markers perform open(\"/FIREBUILD <debug_msg>\", 0) calls\n"
         "                             to let users find unintercepted calls using\n"
         "                             strace or ltrace. This works in debug builds only.\n"
         "   --version              output version information and exit\n"
         "Exit status:\n"
         " exit status of the BUILD COMMAND\n"
         " 1  in case of failure\n");
}


static void export_sorted_locations(const char* configuration_name,
                             const std::string env_var_name,
                             std::map<std::string, std::string>* env) {
  const libconfig::Setting& root = cfg->getRoot();
  const libconfig::Setting& locations_setting = root[configuration_name];
  std::vector<std::string> locations;
  for (int i = 0; i < locations_setting.getLength(); i++) {
    locations.emplace_back(locations_setting[i].c_str());
  }
  if (locations.size() > 0) {
    std::sort(locations.begin(), locations.end());
    std::string locations_appended;
    for (auto loc : locations) {
      if (locations_appended.length() == 0) {
        locations_appended.append(loc);
      } else {
        locations_appended.append(":" + loc);
      }
    }
    (*env)[env_var_name] = std::string(locations_appended);
    FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + env_var_name + "=" + (*env)[env_var_name]);
  }
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
  std::map<std::string, std::string> env;
  for (int i = 0; i < pass_through.getLength(); i++) {
    std::string pass_through_env(pass_through[i].c_str());
    char * got_env = getenv(pass_through_env.c_str());
    if (got_env != NULL) {
      env[pass_through_env] = std::string(got_env);
      FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + std::string(pass_through_env) + "="
               + env[pass_through_env]);
    }
  }
  FB_DEBUG(firebuild::FB_DEBUG_PROC, "");

  FB_DEBUG(firebuild::FB_DEBUG_PROC, "Setting preset environment variables:");
  const libconfig::Setting& preset = root["env_vars"]["preset"];
  for (int i = 0; i < preset.getLength(); i++) {
    std::string str(preset[i].c_str());
    size_t eq_pos = str.find('=');
    if (eq_pos == std::string::npos) {
      firebuild::fb_error("Invalid present environment variable: " + str);
      abort();
    } else {
      const std::string var_name = str.substr(0, eq_pos);
      env[var_name] = str.substr(eq_pos + 1);
      FB_DEBUG(firebuild::FB_DEBUG_PROC, " " + var_name + "=" + env[var_name]);
    }
  }

  export_sorted_locations("system_locations", "FB_SYSTEM_LOCATIONS", &env);
  export_sorted_locations("ignore_locations", "FB_IGNORE_LOCATIONS", &env);

  const char *ld_preload_value = getenv("LD_PRELOAD");
  if (ld_preload_value) {
    env["LD_PRELOAD"] = LIBFIREBUILD_SO ":" + std::string(ld_preload_value);
  } else {
    env["LD_PRELOAD"] = LIBFIREBUILD_SO;
  }
  env["FB_SOCKET"] = fb_conn_string;
  FB_DEBUG(firebuild::FB_DEBUG_PROC, " FB_SOCKET=" + env["FB_SOCKET"]);

  FB_DEBUG(firebuild::FB_DEBUG_PROC, "");

#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    env["FB_INSERT_TRACE_MARKERS"] = "1";
  }
#endif

  char ** ret_env =
      static_cast<char**>(malloc(sizeof(char*) * (env.size() + 1)));

  auto it = env.begin();
  int i = 0;
  while (it != env.end()) {
    ret_env[i] = strdup(std::string(it->first + "=" + it->second).c_str());
    it++;
    i++;
  }
  ret_env[i] = NULL;

  return ret_env;
}

}  /* namespace */

namespace firebuild {

static void reject_exec_child(int fd_conn) {
    FBBCOMM_Builder_scproc_resp sv_msg;
    sv_msg.set_dont_intercept(true);
    sv_msg.set_shortcut(false);

    send_fbb(fd_conn, 0, reinterpret_cast<FBBCOMM_Builder *>(&sv_msg));
}

void accept_exec_child(ExecedProcess* proc, int fd_conn,
                       ProcessTree* proc_tree, int fd0_reopen) {
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

    if (dont_intercept_matcher->match(proc)) {
      /* Executables that should not be intercepted. */
      proc->disable_shortcutting_bubble_up("Executable set to not be intercepted");
      sv_msg.set_dont_intercept(true);
    } else if (dont_shortcut_matcher->match(proc)) {
      if (quirks & FB_QUIRK_LTO_WRAPPER && proc->args().size() > 0 && proc->args()[0] == "make"
          && proc->parent_exec_point()
          && proc->parent_exec_point()->executable()->without_dirs() == "lto-wrapper" ) {
        FB_DEBUG(FB_DEBUG_PROC, "Allow shortcutting lto-wrapper's make (lto-wrapper quirk)");
      } else {
        /* Executables that are known not to be shortcuttable. */
        proc->disable_shortcutting_bubble_up("Executable set to be not shortcut");
      }
    }

    /* Check for executables that we prefer not to shortcut. */
    if (skip_cache_matcher->match(proc)) {
      proc->disable_shortcutting_only_this("Executable matches skip_cache");
    }

    /* If we still potentially can, and prefer to cache / shortcut this process,
     * register the cacher object and calculate the process's fingerprint. */
    if (proc->can_shortcut()) {
      proc->set_cacher(cacher);
      if (!cacher->fingerprint(proc)) {
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
    std::vector<int> fds_appended_to;
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
      for (inherited_file_t& inherited_file : inherited_files) {
        if (inherited_file.type == FD_PIPE_OUT) {
          auto file_fd_old = proc->get_shared_fd(inherited_file.fds[0]);
          auto pipe = file_fd_old->pipe();
          assert(pipe);

          /* As per #689, reopening the pipes causes different behavior than without firebuild. With
           * firebuild, across an exec they no longer share the same "open file description" and thus
           * the fcntl flags. Perform this unduping from the exec parent, i.e. modify the FileFDs to
           * point to a new FileOFD. */
          auto fds = proc->fds();
          int fd = inherited_file.fds[0];
          auto file_fd = std::make_shared<FileFD>(fd, file_fd_old->flags(), pipe,
                                                  file_fd_old->opened_by());
          (*fds)[fd] = file_fd;
          for (size_t i = 1; i < inherited_file.fds.size(); i++) {
            fd = inherited_file.fds[i];
            auto file_fd_dup = std::make_shared<FileFD>(fd, file_fd, false);
            (*fds)[fd] = file_fd_dup;
          }

          /* Create a new unnamed pipe. */
          int fifo_fd[2];
          int ret = pipe2(fifo_fd, file_fd->flags() & ~O_ACCMODE);
          (void)ret;
          assert(ret == 0);
          bump_fd_age(fifo_fd[0]);
          /* The supervisor needs nonblocking fds for the pipes. */
          fcntl(fifo_fd[0], F_SETFL, O_NONBLOCK);

          /* Find the recorders belonging to the parent process. We need to record to all those,
           * plus create a new recorder for ourselves (unless shortcutting is already disabled). */
          auto  recorders =  proc->parent() ? pipe->proc2recorders[proc->parent_exec_point()]
              : std::vector<std::shared_ptr<firebuild::PipeRecorder>>();
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
        }
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
void accept_popen_child(Process* unix_parent, const pending_popen_t *pending_popen) {
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

    if (pipe2(down, flags & ~O_ACCMODE) < 0) {
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
    auto ffd0 = std::make_shared<FileFD>(pending_popen->fd /* client fd */,
                                         (flags & ~O_ACCMODE) | O_RDONLY,
                                         pipe->fd0_shared_ptr(),
                                         unix_parent /* creator */,
                                         true /* close_on_popen */);
    unix_parent->add_filefd(pending_popen->fd /* client fd */, ffd0);

    /* The writing side of this pipe is in the forked and the execed processes.
     * We're lazy and we don't register it for the forked process, no one cares. */
    auto ffd1 = std::make_shared<FileFD>(STDOUT_FILENO /* client fd */,
                                         (flags & ~O_ACCMODE) | O_WRONLY,
                                         pipe->fd1_shared_ptr(),
                                         unix_parent /* creator */,
                                         false /* close_on_popen */);
    proc->add_filefd(STDOUT_FILENO /* client fd */, ffd1);
  } else {
    /* For popen(..., "w") (parent writes -> child reads) create both backing Unix unnamed
     * pipes, as well as the Pipe object handling them. */
    FB_DEBUG(FB_DEBUG_PROC, "This is a popen(..., \"w...\") child");

    if (pipe2(up, flags & ~O_ACCMODE) < 0 || pipe2(down, flags & ~O_ACCMODE) < 0) {
      assert(0 && "pipe2() failed");
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
    auto ffd0 = std::make_shared<FileFD>(STDIN_FILENO /* client fd */,
                                         (flags & ~O_ACCMODE) | O_RDONLY,
                                         pipe->fd0_shared_ptr(),
                                         unix_parent /* creator */,
                                         false /* close_on_popen */);
    proc->add_filefd(STDIN_FILENO /* client fd */, ffd0);

    /* The (so far only) writing side of this pipe is in the popening (parent) process. */
    auto ffd1 = std::make_shared<FileFD>(pending_popen->fd /* client fd */,
                                         (flags & ~O_ACCMODE) | O_WRONLY,
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

  accept_exec_child(proc, pending_popen->child_conn, proc_tree, fd0_reopen);

  proc_tree->DropPendingPopen(unix_parent);
  unix_parent->set_has_pending_popen(false);
}

}  /* namespace firebuild */

namespace {

static void accept_fork_child(firebuild::Process* parent, int parent_fd, int parent_ack,
                              firebuild::Process** child_ref, int pid, int child_fd,
                              int child_ack, firebuild::ProcessTree* proc_tree) {
  TRACK(firebuild::FB_DEBUG_PROC,
        "parent_fd=%s, parent_ack=%d, parent=%s pid=%d child_fd=%s child_ack=%d",
        D_FD(parent_fd), parent_ack, D(parent), pid, D_FD(child_fd), child_ack);

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
void proc_new_process_msg(const FBBCOMM_Serialized *fbbcomm_buf, uint16_t ack_id, int fd_conn,
                          firebuild::Process** new_proc) {
  TRACK(firebuild::FB_DEBUG_PROC, "fd_conn=%s, ack_id=%d", D_FD(fd_conn), ack_id);

  int tag = fbbcomm_buf->get_tag();
  if (tag == FBBCOMM_TAG_scproc_query) {
    const FBBCOMM_Serialized_scproc_query *ic_msg =
        reinterpret_cast<const FBBCOMM_Serialized_scproc_query *>(fbbcomm_buf);
    auto pid = ic_msg->get_pid();
    auto ppid = ic_msg->get_ppid();
    const char* ic_version = ic_msg->get_version();

    if (ic_version && strcmp(ic_version, FIREBUILD_VERSION) != 0) {
      firebuild::fb_error("Mismatched interceptor version: " + std::string(ic_version));
      abort();
    }

    ::firebuild::Process *unix_parent = NULL;
    firebuild::LaunchType launch_type = firebuild::LAUNCH_TYPE_OTHER;
    int type_flags;

    firebuild::Process *parent = NULL;
    std::vector<std::shared_ptr<firebuild::FileFD>>* fds = nullptr;

    /* Locate the parent in case of execve or alike. This includes the
     * case when the outermost intercepted process starts up (no
     * parent will be found) or when this outermost process does an
     * exec (an exec parent will be found then). */
    parent = proc_tree->pid2proc(pid);

    if (parent) {
      /* This PID was already seen, i.e. this process is the result of an exec*(),
       * or a posix_spawn*() where we've already seen and processed the
       * "posix_spawn_parent" message. */
      assert_cmp(parent->state(), !=, firebuild::FB_PROC_FINALIZED);
      if (parent->state() == firebuild::FB_PROC_TERMINATED) {
        fds = parent->pass_on_fds();
      } else {
        /* Queue the ExecedProcess until parent's connection is closed */
        fds = new std::vector<std::shared_ptr<firebuild::FileFD>>();
        auto proc =
            firebuild::ProcessFactory::getExecedProcess(
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
        firebuild::reject_exec_child(fd_conn);
        return;
      }

      /* Verify that the child was expected and get inherited fds. */
      std::vector<std::string> args = ic_msg->get_arg_as_vector();
      fds = unix_parent->pop_expected_child_fds(args, &launch_type, &type_flags);
      if (!fds) {
        fds = new std::vector<std::shared_ptr<firebuild::FileFD>>();
      }

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
        delete fds;
        return;
      }

      /* This is a system or popen child. */

      /* Add a ForkedProcess for the forked child we never directly saw. */
      parent = new firebuild::ForkedProcess(pid, ppid, unix_parent, fds);

      if (launch_type == firebuild::LAUNCH_TYPE_POPEN) {
        /* The new exec child should not inherit the fd connected to the unix_parent's popen()-ed
         * stream. The said fd is not necessarily open. */
        int child_fileno = is_wronly(type_flags) ? STDIN_FILENO : STDOUT_FILENO;
        parent->handle_force_close(child_fileno);

        /* The new exec child also does not inherit parent's popen()-ed fds.
         * See: glibc/libio/iopopen.c:
         *  POSIX states popen shall ensure that any streams from previous popen()
         *  calls that remain open in the parent process should be closed in the new
         *  child process. [...] */
        for (auto& file_fd : *parent->fds()) {
          if (file_fd && file_fd->close_on_popen()) {
            parent->handle_close(file_fd->fd());
          }
        }
      }
      /* For the intermediate ForkedProcess where posix_spawn()'s file_actions were executed,
       * we still had all the fds, even the close-on-exec ones. Now it's time to close them. */
      fds = parent->pass_on_fds();

      parent->set_state(firebuild::FB_PROC_TERMINATED);
      proc_tree->insert(parent);

      /* Now we can ack the previous posix_spawn()'s second message. */
      if (launch_type == firebuild::LAUNCH_TYPE_OTHER) {
        proc_tree->AckParent(unix_parent->pid());
      }
    }

    /* Add the ExecedProcess. */
    auto proc =
        firebuild::ProcessFactory::getExecedProcess(
            ic_msg, parent, fds);
    if (launch_type == firebuild::LAUNCH_TYPE_SYSTEM) {
      unix_parent->set_system_child(proc);
    } else if (launch_type == firebuild::LAUNCH_TYPE_POPEN) {
      /* Entry must have been created at the "popen" message */
      firebuild::pending_popen_t *pending_popen = proc_tree->Proc2PendingPopen(unix_parent);
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
    accept_exec_child(proc, fd_conn, proc_tree);
    *new_proc = proc;

  } else if (tag == FBBCOMM_TAG_fork_child) {
    const FBBCOMM_Serialized_fork_child *ic_msg =
        reinterpret_cast<const FBBCOMM_Serialized_fork_child *>(fbbcomm_buf);
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
                        new_proc, pid, fd_conn, ack_id, proc_tree);
      proc_tree->DropParentAck(ppid);
    }
  }
}

void proc_ic_msg(const FBBCOMM_Serialized *fbbcomm_buf,
                 uint16_t ack_num,
                 int fd_conn,
                 firebuild::Process* proc) {
  TRACKX(firebuild::FB_DEBUG_COMM, 1, 1, firebuild::Process, proc, "fd_conn=%s, tag=%s, ack_num=%d",
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
                          fork_child_sock->sock, fork_child_sock->ack_num, proc_tree);
        proc_tree->DropQueuedForkChild(parent_pid);
      }
      return;
    }
    case FBBCOMM_TAG_execv_failed: {
      // FIXME(rbalint) check execv parameter and record what needs to be
      // checked when shortcutting the process
      proc->set_exec_pending(false);
      break;
    }
    case FBBCOMM_TAG_rusage: {
      const FBBCOMM_Serialized_rusage *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_rusage *>(fbbcomm_buf);
      proc->resource_usage(ic_msg->get_utime_u(), ic_msg->get_stime_u());
      break;
    }
    case FBBCOMM_TAG_system: {
      const FBBCOMM_Serialized_system *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_system *>(fbbcomm_buf);
      assert_null(proc->system_child());
      /* system(cmd) launches a child of argv = ["sh", "-c", cmd] */
      auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
      // FIXME what if !has_cmd() ?
      expected_child->set_sh_c_command(ic_msg->get_cmd());
      expected_child->set_launch_type(firebuild::LAUNCH_TYPE_SYSTEM);
      proc->set_expected_child(expected_child);
      break;
    }
    case FBBCOMM_TAG_system_ret: {
      assert(proc->system_child());
      const FBBCOMM_Serialized_system_ret *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_system_ret *>(fbbcomm_buf);
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
      const FBBCOMM_Serialized_popen *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_popen *>(fbbcomm_buf);
      assert(proc_tree->Proc2PendingPopen(proc) == nullptr);

      int type_flags = ic_msg->get_type_flags();
      auto fds = proc->pass_on_fds(false);
      /* popen(cmd) launches a child of argv = ["sh", "-c", cmd] */
      auto expected_child = new ::firebuild::ExecedProcessEnv(fds);
      // FIXME what if !has_cmd() ?
      expected_child->set_sh_c_command(ic_msg->get_cmd());
      expected_child->set_launch_type(firebuild::LAUNCH_TYPE_POPEN);
      expected_child->set_type_flags(type_flags);
      proc->set_expected_child(expected_child);

      firebuild::pending_popen_t pending_popen;
      pending_popen.type_flags = type_flags;  // FIXME why set it at two places?
      proc_tree->QueuePendingPopen(proc, pending_popen);
      proc->set_has_pending_popen(true);
      break;
    }
    case FBBCOMM_TAG_popen_parent: {
      const FBBCOMM_Serialized_popen_parent *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_popen_parent *>(fbbcomm_buf);
      /* Entry must have been created at the "popen" message */
      firebuild::pending_popen_t *pending_popen = proc_tree->Proc2PendingPopen(proc);
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
      const FBBCOMM_Serialized_popen_failed *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_popen_failed *>(fbbcomm_buf);
      // FIXME what if !has_cmd() ?
      delete(proc->pop_expected_child_fds(
          std::vector<std::string>({"sh", "-c", ic_msg->get_cmd()}),
          nullptr, nullptr, true));
      break;
    }
    case FBBCOMM_TAG_pclose: {
      const FBBCOMM_Serialized_pclose *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_pclose *>(fbbcomm_buf);
      if (!ic_msg->has_error_no()) {
        /* pclose() is essentially an fclose() first, then a waitpid(), but the interceptor
         * sends an extra close message in advance thus here the fd is already tracked as closed. */
        firebuild::ExecedProcess *child =
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
      const FBBCOMM_Serialized_posix_spawn *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_posix_spawn *>(fbbcomm_buf);
      auto expected_child = new ::firebuild::ExecedProcessEnv(proc->pass_on_fds(false));
      std::vector<std::string> argv = ic_msg->get_arg_as_vector();
      expected_child->set_argv(argv);
      proc->set_expected_child(expected_child);
      proc->set_posix_spawn_pending(true);
      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Pre-open the files to be written. */
      for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
        const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
        switch (action->get_tag()) {
          case FBBCOMM_TAG_posix_spawn_file_action_open: {
            /* A successful open to a particular fd, silently closing the previous file if any. */
            const FBBCOMM_Serialized_posix_spawn_file_action_open *action_open =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
            int flags = action_open->get_flags();
            if (is_write(flags)) {
              const firebuild::FileName* file_name = proc->get_absolute(
                  AT_FDCWD, action_open->get_pathname(), action_open->get_pathname_len());
              if (file_name) {
                /* Pretend that the parent opened the file for writing and not the fork child.
                 * This is not accurate, but the fork child does not exist yet. A parallel
                 * process opening the file for writing would disable shortcutting the same way. */
                file_name->open_for_writing(proc);
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
    case FBBCOMM_TAG_posix_spawn_parent: {
      const FBBCOMM_Serialized_posix_spawn_parent *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_parent *>(fbbcomm_buf);

      /* First, do the basic fork() */
      auto pid = ic_msg->get_pid();
      auto fork_child = firebuild::ProcessFactory::getForkedProcess(pid, proc);
      proc_tree->insert(fork_child);

      /* The actual forked process might perform some file operations according to
       * posix_spawn()'s file_actions. Do the corresponding administration. */
      for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
        const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
        switch (action->get_tag()) {
          case FBBCOMM_TAG_posix_spawn_file_action_open: {
            /* A successful open to a particular fd, silently closing the previous file if any. */
            const FBBCOMM_Serialized_posix_spawn_file_action_open *action_open =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
            const char *pathname = action_open->get_pathname();
            const size_t pathname_len = action_open->get_pathname_len();
            int fd = action_open->get_fd();
            int flags = action_open->get_flags();
            mode_t mode = action_open->get_mode();
            fork_child->handle_force_close(fd);
            fork_child->handle_open(AT_FDCWD, pathname, pathname_len, flags, mode, fd, 0, -1, 0,
                                    false, false);
            /* Revert the effect of "pre-opening" paths to be written in the posix_spawn message.*/
            if (is_write(flags)) {
              const firebuild::FileName* file_name = fork_child->get_absolute(AT_FDCWD, pathname,
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
            const FBBCOMM_Serialized_posix_spawn_file_action_close *action_close =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_close *>(action);
            int fd = action_close->get_fd();
            fork_child->handle_force_close(fd);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_closefrom: {
            /* A successful closefrom. */
            const FBBCOMM_Serialized_posix_spawn_file_action_closefrom *action_closefrom =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_closefrom *>
                (action);
            int lowfd = action_closefrom->get_lowfd();
            fork_child->handle_closefrom(lowfd);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_dup2: {
            /* A successful dup2.
             * Note that as per https://austingroupbugs.net/view.php?id=411 and glibc's
             * implementation, oldfd==newfd clears the close-on-exec bit (here only,
             * not in a real dup2()). */
            const FBBCOMM_Serialized_posix_spawn_file_action_dup2 *action_dup2 =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_dup2 *>(action);
            int oldfd = action_dup2->get_oldfd();
            int newfd = action_dup2->get_newfd();
            if (oldfd == newfd) {
              fork_child->handle_clear_cloexec(oldfd);
            } else {
              fork_child->handle_dup3(oldfd, newfd, 0, 0);
            }
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_chdir: {
            /* A successful chdir. */
            const FBBCOMM_Serialized_posix_spawn_file_action_chdir *action_chdir =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_chdir *>(action);
            const char *pathname = action_chdir->get_pathname();
            fork_child->handle_set_wd(pathname);
            break;
          }
          case FBBCOMM_TAG_posix_spawn_file_action_fchdir: {
            /* A successful fchdir. */
            const FBBCOMM_Serialized_posix_spawn_file_action_fchdir *action_fchdir =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_fchdir *>(action);
            int fd = action_fchdir->get_fd();
            fork_child->handle_set_fwd(fd);
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
        std::vector<std::string> arg = ic_msg->get_arg_as_vector();
        delete(proc->pop_expected_child_fds(arg, nullptr));
        fork_child->set_exec_pending(true);
      }
      fork_child->set_state(firebuild::FB_PROC_TERMINATED);
      /* In either case, ACK the "posix_spawn_parent" message, don't necessarily wait for the
       * child to appear. */
      break;
    }
    case FBBCOMM_TAG_posix_spawn_failed: {
      const FBBCOMM_Serialized_posix_spawn_failed *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_failed *>(fbbcomm_buf);
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
            const FBBCOMM_Serialized_posix_spawn_file_action_open *action_open =
                reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
            int flags = action_open->get_flags();
            if (is_write(flags)) {
              const firebuild::FileName* file_name = proc->get_absolute(
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
      const FBBCOMM_Serialized_wait *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_wait *>(fbbcomm_buf);
      const int pid = ic_msg->get_pid();
      firebuild::Process *child = proc_tree->pid2proc(pid);
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
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_pipe_request *>(fbbcomm_buf), fd_conn);
      break;
    }
    case FBBCOMM_TAG_pipe_fds: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_pipe_fds *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_execv: {
      const FBBCOMM_Serialized_execv *ic_msg =
          reinterpret_cast<const FBBCOMM_Serialized_execv *>(fbbcomm_buf);
      proc->update_rusage(ic_msg->get_utime_u(), ic_msg->get_stime_u());
      // FIXME(rbalint) save execv parameters
      proc->set_exec_pending(true);
      break;
    }
    case FBBCOMM_TAG_pre_open: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_pre_open *>(fbbcomm_buf));
     break;
    }
    case FBBCOMM_TAG_open: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_open *>(fbbcomm_buf), fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_freopen: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_freopen *>(fbbcomm_buf), fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_dlopen: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_dlopen *>(fbbcomm_buf), fd_conn, ack_num);
      /* ACK is sent by the msg handler if needed. */
      return;
    }
    case FBBCOMM_TAG_close: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_close *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_closefrom: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_closefrom *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_close_range: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_close_range *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_truncate: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_truncate *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_unlink: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_unlink *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_mkdir: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_mkdir *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_rmdir: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_rmdir *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_dup3: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_dup3 *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_dup: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_dup *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_rename: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_rename *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_symlink: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_symlink *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_fcntl: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_fcntl *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_ioctl: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_ioctl *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_umask: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_umask *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_chdir: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_chdir *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_fchdir: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_fchdir *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_read_from_inherited: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_read_from_inherited *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_write_to_inherited: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_write_to_inherited *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_seek_in_inherited: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_seek_in_inherited *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_recvmsg_scm_rights: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_recvmsg_scm_rights *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_link: {
      proc->exec_point()->disable_shortcutting_bubble_up("Creating a hard link is not supported");
      break;
    }
    case FBBCOMM_TAG_fstatat: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_fstatat *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_faccessat: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_faccessat *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_fchmodat: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_fchmodat *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_memfd_create: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_memfd_create *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_timerfd_create: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_timerfd_create *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_epoll_create: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_epoll_create *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_eventfd: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_eventfd *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_signalfd: {
      ::firebuild::ProcessFBBAdaptor::handle(proc,
          reinterpret_cast<const FBBCOMM_Serialized_signalfd *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_getrandom: {
      auto *ic_msg = reinterpret_cast<const FBBCOMM_Serialized_getrandom *>(fbbcomm_buf);
      const unsigned int flags = ic_msg->get_flags_with_fallback(0);
      const std::string pathname(flags & GRND_RANDOM ? "/dev/random" : "/dev/urandom");
      if (!firebuild::FileName::Get(pathname)->is_in_ignore_location()) {
        proc->exec_point()->disable_shortcutting_bubble_up(
            ("Using " + pathname + " is not allowed").c_str());
      }
      break;
    }
    case FBBCOMM_TAG_futime: {
      auto *ic_msg = reinterpret_cast<const FBBCOMM_Serialized_futime *>(fbbcomm_buf);
      const int fd = ic_msg->get_fd();
      const firebuild::FileFD* ffd = proc->get_fd(fd);
      if (!ic_msg->has_error_no() && ffd && is_write(ffd->flags()) && ic_msg->get_all_utime_now()) {
        /* The fd has been opened for writing and the access and modification times should be set to
         * current time which happens automatically when the process is shortcut. This is safe. */
      } else {
        firebuild::ExecedProcess* next_exec_level;
        if (firebuild::quirks & FB_QUIRK_LTO_WRAPPER && proc->exec_point()->args().size() > 0
            && proc->exec_point()->args()[0] == "touch"
            && (next_exec_level = proc->parent_exec_point())  // sh
            && (next_exec_level = next_exec_level->parent_exec_point())  // make
            && (next_exec_level = next_exec_level->parent_exec_point())  // lto-wrapper
            && next_exec_level->executable()->without_dirs() == "lto-wrapper" ) {
          FB_DEBUG(firebuild::FB_DEBUG_PROC, "Allow shortcutting lto-wrapper's touch descendant "
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
    case FBBCOMM_TAG_clone: {
      proc->exec_point()->disable_shortcutting_bubble_up("clone() is not supported");
      break;
    }
    case FBBCOMM_TAG_socket: {
      ::firebuild::ProcessFBBAdaptor::handle(
           proc, reinterpret_cast<const FBBCOMM_Serialized_socket *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_socketpair: {
      ::firebuild::ProcessFBBAdaptor::handle(
           proc, reinterpret_cast<const FBBCOMM_Serialized_socketpair *>(fbbcomm_buf));
      break;
    }
    case FBBCOMM_TAG_fb_debug:
    case FBBCOMM_TAG_fb_error:
    case FBBCOMM_TAG_fchownat:
    case FBBCOMM_TAG_fpathconf:
    case FBBCOMM_TAG_getdomainname:
    case FBBCOMM_TAG_gethostname:
    case FBBCOMM_TAG_lockf:
    case FBBCOMM_TAG_pathconf:
    case FBBCOMM_TAG_readlink:
    case FBBCOMM_TAG_scproc_resp:
    case FBBCOMM_TAG_syscall:
    case FBBCOMM_TAG_sysconf:
      {
      // TODO(rbalint)
      break;
    }
    case FBBCOMM_TAG_gen_call: {
      // TODO(rbalint) disable shortcutting after checking the performance impact
      // and handling often used calls separately
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
} /* NOLINT(readability/fn_size) */


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

  FILE* src_file = fopen((datadir + "/" + html_orig_filename).c_str(), "r");
  if (src_file == NULL) {
    firebuild::fb_perror("fopen");
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

  /* export profile */
  {
    FILE* dot = fopen((dir + "/" + dot_filename).c_str(), "w");
    if (dot == NULL) {
      firebuild::fb_perror("fopen");
      firebuild::fb_error("Failed to open dot file for writing profile graph.");
    }
    proc_tree->export_profile2dot(dot);
    fclose(dot);
  }

  auto system_cmd =
      dot_cmd + " -Tsvg " + dir + "/" + dot_filename
      + " | sed 's/viewBox=\\\"[^\\\"]*\\\" //' > " + dir + "/" + svg_filename;
  if (system(system_cmd.c_str()) != 0) {
    firebuild::fb_perror("system");
    firebuild::fb_error("Failed to generate profile graph with the following command: "
                        + system_cmd);
  }

  FILE* dst_file = fopen(html_filename.c_str(), "w");
  int ret = dst_file == NULL ? -1 : 0;
  while ((ret != -1)) {
    char* line = NULL;
    size_t zero = 0;
    if (getline(&line, &zero, src_file) == -1) {
      /* finished reading file */
      if (!feof(src_file)) {
        firebuild::fb_perror("getline");
        firebuild::fb_error("Reading from report template failed.");
      }
      free(line);
      break;
    }
    if (strstr(line, d3_filename) != NULL) {
      int d3 = open((d3_datadir + "/" + d3_filename).c_str(), O_RDONLY);
      if (d3 == -1) {
        /* File is not available locally, use the online version. */
        fprintf(dst_file, "<script type=\"text/javascript\" "
                "src=\"https://firebuild.io/d3.v5.min.js\"></script>\n");
        fflush(dst_file);
      } else {
        fprintf(dst_file, "<script type=\"text/javascript\">\n");
        fflush(dst_file);
        ret = sendfile_full(fileno(dst_file), d3);
        fsync(fileno(dst_file));
        fprintf(dst_file, "    </script>\n");
        close(d3);
      }
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

}  /* namespace */

/**
 * Create connection sockets for the interceptor
 */
static int create_listener() {
  int listener;

  if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    firebuild::fb_perror("socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_un local;
  local.sun_family = AF_UNIX;
  strncpy(local.sun_path, fb_conn_string, sizeof(local.sun_path) - 1);

  if (bind(listener, (struct sockaddr *)&local, sizeof(local)) == -1) {
    firebuild::fb_perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(listener, 500) == -1) {
    firebuild::fb_perror("listen");
    exit(EXIT_FAILURE);
  }
  return listener;
}

static void ic_conn_readcb(const struct epoll_event* event, void *ctx) {
  auto conn_ctx = reinterpret_cast<firebuild::ConnectionContext*>(ctx);
  auto proc = conn_ctx->proc;
  auto &buf = conn_ctx->buffer();
  size_t full_length;
  const msg_header * header;
  firebuild::ProcessDebugSuppressor debug_suppressor(proc);

  int read_ret = buf.read(firebuild::Epoll::event_fd(event), -1);
  if (read_ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      /* Try again later. */
      return;
    }
  }
  if (read_ret <= 0) {
    FB_DEBUG(firebuild::FB_DEBUG_COMM, "socket " +
             firebuild::d_fd(firebuild::Epoll::event_fd(event)) +
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
    const FBBCOMM_Serialized *fbbcomm_msg =
        reinterpret_cast<const FBBCOMM_Serialized *>(buf.data() + sizeof(*header));

    if (!proc) {
      /* Now the message is complete, the debug suppression can be correctly set. */
      firebuild::debug_suppressed =
          firebuild::ProcessFactory::peekProcessDebuggingSuppressed(fbbcomm_msg, proc_tree);
    }

    if (FB_DEBUGGING(firebuild::FB_DEBUG_COMM)) {
      if (!firebuild::debug_suppressed) {
        FB_DEBUG(firebuild::FB_DEBUG_COMM, "fd " + firebuild::d_fd(
            firebuild::Epoll::event_fd(event)) + ": (" + d(proc) + ")");
        if (header->ack_id) {
          fprintf(stderr, "ack_num: %d\n", header->ack_id);
        }
        fbbcomm_msg->debug(stderr);
        fflush(stderr);
      }
    }

    /* Process the messaage. */
    if (proc) {
      proc_ic_msg(fbbcomm_msg, header->ack_id, firebuild::Epoll::event_fd(event), proc);
    } else {
      /* Fist interceptor message */
      proc_new_process_msg(fbbcomm_msg, header->ack_id, firebuild::Epoll::event_fd(event),
                           &conn_ctx->proc);
      /* Reset suppression which was set peeking at the message. */
      firebuild::debug_suppressed = false;
    }
    buf.discard(full_length);
  } while (buf.length() > 0);
}


static void save_child_status(pid_t pid, int status, int * ret, bool orphan) {
  TRACK(firebuild::FB_DEBUG_PROC, "pid=%d, status=%d, orphan=%s", pid, status, D(orphan));

  if (WIFEXITED(status)) {
    *ret = WEXITSTATUS(status);
    firebuild::Process* proc = proc_tree->pid2proc(pid);
    if (proc && proc->fork_point()) {
      proc->fork_point()->set_exit_status(*ret);
    }
    FB_DEBUG(firebuild::FB_DEBUG_COMM, std::string(orphan ? "orphan" : "child")
             + " process exited with status " + std::to_string(*ret) + ". ("
             + d(proc_tree->pid2proc(pid)) + ")");
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr, "%s process has been killed by signal %d\n",
            orphan ? "Orphan" : "Child",
            WTERMSIG(status));
  }
}


/* This is the installed handler for SIGCHLD, using the good ol' self-pipe trick to cooperate with
 * epoll_wait() without race condition. Our measurements show this is faster than epoll_pwait(). */
static void sigchild_handler(int signum) {
  (void)signum;  /* unused */
  /* listener being -1 means that we're already exiting, and might have closed sigchild_selfpipe.
   * In case an orphan descendant dies now and we get a SIGCHLD, just ignore it. */
  if (listener >= 0) {
    char dummy = 0;
    int write_ret = write(sigchild_selfpipe[1], &dummy, 1);
    (void)write_ret;  /* unused */
  }
}

/* This is the actual business logic for SIGCHLD, called synchronously when processing the events
 * returned by epoll_wait(). */
static void sigchild_cb(const struct epoll_event* event, void *arg) {
  TRACK(firebuild::FB_DEBUG_PROC, "");

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
      firebuild::Process* proc = proc_tree->pid2proc(child_pid);
      assert(proc);
      firebuild::ProcessDebugSuppressor debug_suppressor(proc);
      save_child_status(waitpid_ret, status, &child_ret, false);
      proc->set_been_waited_for();
    } else if (waitpid_ret > 0) {
      /* This is an orphan process. Its fork parent quit without wait()-ing for it
       * and as a subreaper the supervisor received the SIGCHLD for it. */
      firebuild::Process* proc = proc_tree->pid2proc(waitpid_ret);
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
    epoll->del_fd(listener);
    close(listener);
    listener = -1;
  }
}


static void accept_ic_conn(const struct epoll_event* event, void *arg) {
  TRACK(firebuild::FB_DEBUG_COMM, "listener=%d", listener);

  struct sockaddr_storage remote;
  socklen_t slen = sizeof(remote);
  (void) event; /* unused */
  (void) arg;   /* unused */

  int fd = accept(listener, (struct sockaddr*)&remote, &slen);
  if (fd < 0) {
    firebuild::fb_perror("accept");
  } else {
    firebuild::bump_fd_age(fd);
    auto conn_ctx = new firebuild::ConnectionContext(proc_tree, fd);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    epoll->add_fd(fd, EPOLLIN, ic_conn_readcb, conn_ctx);
  }
}

static bool running_under_valgrind() {
  const char *v = getenv("LD_PRELOAD");
  if (v == NULL) {
    return false;
  } else {
    return (strstr(v, "/valgrind/") != NULL || strstr(v, "/vgpreload") != NULL);
  }
}


int main(const int argc, char *argv[]) {
  char *config_file = NULL;
  char *directory = NULL;
  std::list<std::string> config_strings = {};
  int c;

  /* running under BATS fd 3 is inherited */
  if (fcntl(3, F_GETFD) != -1 || errno != EBADF) {
    bats_inherited_fd = 3;
  }

  /* init global data */
  cfg = new libconfig::Config();

  /* parse options */
  setenv("POSIXLY_CORRECT", "1", true);
  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      {"config-file",          required_argument, 0, 'c' },
      {"directory",            required_argument, 0, 'C' },
      {"debug-flags",          required_argument, 0, 'd' },
      {"debug-filter",         required_argument, 0, 'D' },
      {"generate-report",      optional_argument, 0, 'r' },
      {"help",                 no_argument,       0, 'h' },
      {"option",               required_argument, 0, 'o' },
      {"insert-trace-markers", no_argument,       0, 'i' },
      {"version",              no_argument,       0, 'v' },
      {0,                                0,       0,  0  }
    };

    c = getopt_long(argc, argv, "c:C:d:D:r::o:hi",
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
      /* Merge the values, so that multiple '-d' options are also allowed. */
      firebuild::debug_flags |= firebuild::parse_debug_flags(optarg);
      break;

    case 'D':
      firebuild::init_debug_filter(optarg);
      break;

    case 'h':
      usage();
      exit(EXIT_SUCCESS);
      /* break; */

    case 'o':
      if (optarg != NULL) {
        config_strings.push_back(std::string(optarg));
      } else {
        usage();
        exit(EXIT_FAILURE);
      }
      break;

    case 'i':
#ifdef FB_EXTRA_DEBUG
      insert_trace_markers = true;
#endif
      break;

    case 'r':
      generate_report = true;
      if (optarg != NULL) {
        report_file = optarg;
      }
      break;

    case 'v':
      printf("Firebuild " FIREBUILD_VERSION "\n\n"
             "This is an unpublished work. All rights reserved.\n");
      exit(EXIT_SUCCESS);
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

  if (FB_DEBUGGING(firebuild::FB_DEBUG_TIME)) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
  }

  firebuild::read_config(cfg, config_file, config_strings);

  /* Initialize the cache */
  std::string cache_dir;
  char* cache_dir_env;
  if ((cache_dir_env = getenv("FIREBUILD_CACHE_DIR")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env);
  } else if ((cache_dir_env = getenv("XDG_CACHE_HOME")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env) + "/firebuild";
  } else if ((cache_dir_env = getenv("HOME")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env) + "/.cache/firebuild";
  } else {
    firebuild::fb_error("Please set HOME or XDG_CACHE_HOME or FIREBUILD_CACHE_DIR to let "
                        "firebuild place the cache somewhere.");
    exit(EXIT_FAILURE);
  }

  struct stat st;
  if (stat(cache_dir.c_str(), &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      firebuild::fb_error("cache dir exists but is not a directory");
      exit(EXIT_FAILURE);
    }
  } else {
    if (mkdirhier(cache_dir.c_str(), 0700) != 0) {
      firebuild::fb_perror("mkdir");
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

  firebuild::PipeRecorder::set_base_dir((cache_dir + "/tmp").c_str());
  firebuild::hash_cache = new firebuild::HashCache();

  {
    char *pattern;
    if (asprintf(&pattern, "%s/firebuild.XXXXXX", get_tmpdir()) < 0) {
      firebuild::fb_perror("asprintf");
    }
    fb_tmp_dir = mkdtemp(pattern);
    if (fb_tmp_dir == NULL) {
      firebuild::fb_perror("mkdtemp");
      exit(EXIT_FAILURE);
    }
    fb_conn_string = strdup((std::string(fb_tmp_dir) + "/socket").c_str());
  }

  firebuild::FileName::default_tmpdir = firebuild::FileName::Get("/tmp", strlen("/tmp"));
  auto env_exec = get_sanitized_env();

  /* Set up sigchild handler */
  if (pipe2(sigchild_selfpipe, O_CLOEXEC | O_NONBLOCK) != 0) {
    firebuild::fb_perror("pipe");
    exit(EXIT_FAILURE);
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchild_handler;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  /* Configure epoll */
  epoll = new firebuild::Epoll(EPOLL_CLOEXEC);

  /* Open listener socket before forking child to always let the child connect */
  listener = create_listener();
  epoll->add_fd(listener, EPOLLIN, accept_ic_conn, NULL);

  /* Collect orphan children */
  prctl(PR_SET_CHILD_SUBREAPER, 1);

  /* run command and handle interceptor messages */
  if ((child_pid = fork()) == 0) {
    int i;
    /* intercepted process */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
    char* argv_exec[argc - optind + 1];
#pragma GCC diagnostic pop

    /* we don't need that */
    close(listener);

    /* create and execute build command */
    for (i = 0; i < argc - optind ; i++) {
      argv_exec[i] = argv[optind + i];
    }
    argv_exec[i] = NULL;

    if (directory != NULL && chdir(directory) != 0) {
      firebuild::fb_perror("chdir");
      exit(EXIT_FAILURE);
    }

    execvpe(argv[optind], argv_exec, env_exec);
    firebuild::fb_perror("Executing build command failed");
    exit(EXIT_FAILURE);
  } else {
    /* supervisor process */

    /* This creates some Pipe objects, so needs ev_base being set up. */
    proc_tree = new firebuild::ProcessTree();

    /* Add a ForkedProcess for the supervisor's forked child we never directly saw. */
    proc_tree->insert_root(child_pid, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

    bump_limits();
    /* no SIGPIPE if a supervised process we're writing to unexpectedly dies */
    signal(SIGPIPE, SIG_IGN);

    epoll->add_fd(sigchild_selfpipe[0], EPOLLIN, sigchild_cb, NULL);

    /* Main loop for processing interceptor messages */
    while (listener >= 0) {
      /* This is where the process spends its idle time: waiting for an event over a fd, or a
       * sigchild.
       *
       * If our immediate child exited (rather than some orphan descendant thereof, see
       * prctl(PR_SET_CHILD_SUBREAPER) above) then the handler sigchild_cb() will set listener to
       * -1, that's how we'll break out of this loop. */
      epoll->wait();

      /* Process the reported events, if any. */
      epoll->process_all_events();
    }

    /* Finish all top pipes */
    proc_tree->FinishInheritedFdPipes();
    /* Close the self-pipe */
    close(sigchild_selfpipe[0]);
    close(sigchild_selfpipe[1]);
  }

  if (firebuild::debug_filter) {
    firebuild::debug_suppressed = false;
  }

  if (!proc_tree->root()) {
    fprintf(stderr, "ERROR: Could not collect any information about the build "
            "process\n");
    child_ret = EXIT_FAILURE;
  } else {
    /* Print times, including user and sys time separately for firebuild itself and its children.
     * The syntax is similar to bash's "time", although easier to parse (raw seconds in decimal). */
    if (FB_DEBUGGING(firebuild::FB_DEBUG_TIME)) {
      struct timespec end_time, diff_time;
      struct rusage ru_myslf, ru_chldr, ru_total;

      clock_gettime(CLOCK_MONOTONIC, &end_time);
      getrusage(RUSAGE_SELF, &ru_myslf);
      getrusage(RUSAGE_CHILDREN, &ru_chldr);

      timespecsub(&end_time, &start_time, &diff_time);
      timeradd(&ru_myslf.ru_utime, &ru_chldr.ru_utime, &ru_total.ru_utime);
      timeradd(&ru_myslf.ru_stime, &ru_chldr.ru_stime, &ru_total.ru_stime);

      fprintf(stderr, "\nResource usages, in seconds:\n"
                      "real           %5ld.%03ld\n"
                      "user firebuild %5ld.%03ld\n"
                      "user children  %5ld.%03ld\n"
                      "user total     %5ld.%03ld\n"
                      "sys  firebuild %5ld.%03ld\n"
                      "sys  children  %5ld.%03ld\n"
                      "sys  total     %5ld.%03ld\n",
                      diff_time.tv_sec, diff_time.tv_nsec / (1000 * 1000),
                      ru_myslf.ru_utime.tv_sec, ru_myslf.ru_utime.tv_usec / 1000,
                      ru_chldr.ru_utime.tv_sec, ru_chldr.ru_utime.tv_usec / 1000,
                      ru_total.ru_utime.tv_sec, ru_total.ru_utime.tv_usec / 1000,
                      ru_myslf.ru_stime.tv_sec, ru_myslf.ru_stime.tv_usec / 1000,
                      ru_chldr.ru_stime.tv_sec, ru_chldr.ru_stime.tv_usec / 1000,
                      ru_total.ru_stime.tv_sec, ru_total.ru_stime.tv_usec / 1000);
    }

    /* show process tree if needed */
    if (generate_report) {
      const std::string datadir(getenv("FIREBUILD_DATA_DIR") ? getenv("FIREBUILD_DATA_DIR")
                                : FIREBUILD_DATADIR);
      write_report(report_file, datadir);
    }
  }

  unlink(fb_conn_string);
  rmdir(fb_tmp_dir);

  if (running_under_valgrind()) {
    /* keep Valgrind happy */
    {
      char* env_str;
      for (int i = 0; (env_str = env_exec[i]) != NULL; i++) {
        free(env_str);
      }
      free(env_exec);
    }

    /* No more epoll needed, this also closes all tracked fds */
    delete epoll;
    free(fb_conn_string);
    free(fb_tmp_dir);
    delete(proc_tree);
    delete(cfg);
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    if (bats_inherited_fd > -1) {
      close(bats_inherited_fd);
    }
  }

  exit(child_ret);
}

namespace firebuild {

/** Print error message */
extern void fb_error(const std::string &msg) {
  fprintf(stderr, "FIREBUILD ERROR: %s\n", msg.c_str());
}

/** Print debug message */
extern void fb_debug(const std::string &msg) {
  if (!debug_suppressed) {
    fprintf(stderr, "FIREBUILD: %s\n", msg.c_str());
  }
}

int32_t debug_flags = 0;

}  /* namespace firebuild */
