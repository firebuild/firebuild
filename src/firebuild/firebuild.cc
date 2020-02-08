/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <libgen.h>

#include <string>
#include <iostream>
#include <cerrno>
#include <cstdio>
#include <stdexcept>

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <libconfig.h++>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/cache.h"
#include "firebuild/multi_cache.h"
#include "firebuild/process_factory.h"
#include "firebuild/process_tree.h"
#include "firebuild/process_proto_adaptor.h"
#include "fb-messages.pb.h"

/* Globals */
firebuild::Cache *cache;
firebuild::MultiCache *multi_cache;

namespace {

static char global_cfg[] = "/etc/firebuildrc";

static char datadir[] = FIREBUILD_DATADIR;

static char *fb_tmp_dir;
/** The connection sockets are derived from the fb_conn_string by appending an integer. */
static std::string fb_conn_string;

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
static int child_pid, child_ret = 1;
static google::protobuf::io::FileOutputStream * error_fos;
static bool insert_trace_markers = false;
static bool generate_report = false;
static const char *report_file = "firebuild-build-report.html";
static firebuild::ProcessTree *proc_tree;

/** global configuration */
libconfig::Config * cfg;

static void usage() {
  printf("Usage: firebuild [OPTIONS] <BUILD COMMAND>\n"
         "Execute BUILD COMMAND with FireBuild™ instrumentation\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "   -c --config-file=FILE     use FILE as configuration file\n"
         "   -d --debug-level=N        set debugging level to N (0-4, default is 0)\n"
         "   -r --generate-report[=HTML] generate a report on the build command execution.\n"
         "                             the report's filename can be specified \n"
         "                             (firebuild-build-report.html by default). \n"
         "   -h --help                 show this help\n"
         "   -i --insert-trace-markers perform open(\"/FIREBUILD <debug_msg>\", 0) calls\n"
         "                             to let users find unintercepted calls using\n"
         "                             strace or ltrace\n"
         "Exit status:\n"
         " exit status of the BUILD COMMAND\n"
         " 1  in case of failure\n");
}

/** Parse configuration file */
static void parse_cfg_file(const char * const custom_cfg_file) {
  // we fall back to global configuration file
  std::string cfg_file(global_cfg);
  if (custom_cfg_file != NULL) {
    cfg_file = std::string(custom_cfg_file);
  } else {
    char * homedir = getenv("HOME");
    int cfg_fd;
    if ((homedir != NULL ) &&
        (-1 != (cfg_fd = open(std::string(homedir +
                                          std::string("/.firebuildrc")).c_str(),
                              O_RDONLY)))) {
      // fall back to private config file
      cfg_file = std::string(homedir + std::string("/.firebuildrc"));
      close(cfg_fd);
    }
  }
  try {
    cfg->readFile(cfg_file.c_str());
  }
  catch(const libconfig::FileIOException &fioex) {
    std::cerr << "Could not read configuration file " << cfg_file << std::endl;
    exit(EXIT_FAILURE);
  }
  catch(const libconfig::ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
              << " - " << pex.getError() << std::endl;
    exit(EXIT_FAILURE);
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

  FB_DEBUG(1, "Passing through environment variables:");

  const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
  std::vector<std::string> env_v;
  for (int i = 0; i < pass_through.getLength(); i++) {
    char * got_env = getenv(pass_through[i].c_str());
    if (NULL  != got_env) {
      env_v.push_back(pass_through[i].c_str() +
                      std::string("=") +
                      std::string(got_env));
      FB_DEBUG(1, " " + env_v.back());
    }
  }
  FB_DEBUG(1, "");

  FB_DEBUG(1, "Setting preset environment variables:");
  const libconfig::Setting& preset = root["env_vars"]["preset"];
  for (int i = 0; i < preset.getLength(); i++) {
    env_v.push_back(preset[i]);
    FB_DEBUG(1, " " + env_v.back());
  }
  env_v.push_back("FB_SOCKET=" + fb_conn_string);
  FB_DEBUG(1, " " + env_v.back());

  FB_DEBUG(1, "");

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

  if (NULL == ((sigchld_stream = fdopen(sigchld_fds[1], "w")))) {
    perror("fdopen");
    exit(EXIT_FAILURE);
  }

  if (0 != setvbuf(sigchld_stream, NULL, _IONBF, 0)) {
    perror("setvbuf");
    exit(EXIT_FAILURE);
  }
}

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 */
void ack_msg(const int conn, const int ack_num) {
  firebuild::msg::SupervisorMsg sv_msg;
  sv_msg.set_ack_num(ack_num);
  FB_DEBUG(4, "sending ACK no. " + std::to_string(ack_num));
  firebuild::fb_send_msg(sv_msg, conn);
  FB_DEBUG(4, "ACK sent");
}

/**
 * Process message coming from interceptor
 * @param fb_conn file desctiptor of the connection
 */
void proc_ic_msg(const firebuild::msg::InterceptorMsg &ic_msg,
                 const int fd_conn) {
  if (ic_msg.has_scproc_query()) {
    auto scq = ic_msg.scproc_query();
    firebuild::msg::SupervisorMsg sv_msg;
    auto scproc_resp = sv_msg.mutable_scproc_resp();

    firebuild::Process *parent = NULL;

    /* Locate the parent in case of execve or alike. This includes the
     * case when the outermost intercepted process starts up (no
     * parent will be found) or when this outermost process does an
     * exec (an exec parent will be found then). */
    parent = proc_tree->pid2proc(scq.pid());

    /* Locate the parent in case of system/popen/posix_spawn, but not
     * when the first intercepter process starts up. */
    if (!parent && scq.ppid() != getpid()) {
      /* Locate the indirect parent who called system/popen/posix_spawn. */
      ::firebuild::Process *unix_parent = proc_tree->pid2proc(scq.ppid());
      assert(unix_parent != NULL);

      /* Add a ForkedProcess for the forked child we never directly saw. */
      parent = new firebuild::ForkedProcess(scq.pid(), scq.ppid(), unix_parent);
      parent->set_state(firebuild::FB_PROC_FINISHED);
      proc_tree->insert(parent, -1);

      /* Verify that the child was expected. */
      ::firebuild::ExecedProcessParameters expected_child;
      for (const auto &arg : ic_msg.scproc_query().arg()) {
        expected_child.argv().push_back(arg);
      }
      if (!unix_parent->remove_expected_child(expected_child)) {
        firebuild::fb_error("Unexpected system/popen/posix_spawn child appeared: " + to_string(expected_child));
        // FIXME now what?
      }
    }

    /* Add the ExecedProcess. */
    auto proc =
        firebuild::ProcessFactory::getExecedProcess(
            ic_msg.scproc_query(), parent);
    proc_tree->insert(proc, fd_conn);
    // TODO(rbalint) look up stored result
#if 0
    if ( /* can shortcut*/) {
      scproc_resp->set_shortcut(true);
      scproc_resp->set_exit_status(0);
    } else {
#endif
      scproc_resp->set_shortcut(false);
      if (firebuild::debug_level != 0) {
        scproc_resp->set_debug_level(firebuild::debug_level);
      }
#if 0
    }
#endif
    firebuild::fb_send_msg(sv_msg, fd_conn);
  } else if (ic_msg.has_fork_child()) {
    ::firebuild::Process *pproc = NULL;
    pproc = proc_tree->pid2proc(ic_msg.fork_child().ppid());
    /* record new process */
    auto proc =
        firebuild::ProcessFactory::getForkedProcess(ic_msg.fork_child(), pproc);
    proc_tree->insert(proc, fd_conn);
  } else if (ic_msg.has_execvfailed()) {
    auto *proc = proc_tree->Sock2Proc(fd_conn);
    // FIXME(rbalint) check execv parameter and record what needs to be
    // checked when shortcutting the process
    proc = proc;
  } else if (ic_msg.has_exit() ||
             ic_msg.has_execv() ||
             ic_msg.has_system() ||
             ic_msg.has_system_ret() ||
             ic_msg.has_popen() ||
             ic_msg.has_popen_parent() ||
             ic_msg.has_popen_failed() ||
             ic_msg.has_posix_spawn() ||
             ic_msg.has_posix_spawn_parent() ||
             ic_msg.has_posix_spawn_failed() ||
             ic_msg.has_open() ||
             ic_msg.has_close() ||
             ic_msg.has_pipe2() ||
             ic_msg.has_dup3() ||
             ic_msg.has_dup() ||
             ic_msg.has_fcntl() ||
             ic_msg.has_chdir() ||
             ic_msg.has_fdopendir() ||
             ic_msg.has_opendir()) {
    try {
      ::firebuild::Process *proc = proc_tree->Sock2Proc(fd_conn);
      if (ic_msg.has_exit()) {
        proc->exit_result(ic_msg.exit().exit_status(),
                          ic_msg.exit().utime_u(),
                          ic_msg.exit().stime_u());
      } else if (ic_msg.has_system()) {
        proc->add_running_system_cmd(ic_msg.system().cmd());

        // system(cmd) launches a child of argv = ["sh", "-c", cmd]
        ::firebuild::ExecedProcessParameters expected_child;
        // FIXME what if !has_cmd() ?
        expected_child.set_sh_c_command(ic_msg.system().cmd());
        proc->add_expected_child(expected_child);
      } else if (ic_msg.has_system_ret()) {
        if (!proc->remove_running_system_cmd(ic_msg.system_ret().cmd())) {
          firebuild::fb_error("system(\"" + ic_msg.system_ret().cmd()
                              + "\") exited but the call was not registered ");
        }
      } else if (ic_msg.has_popen()) {
        // popen(cmd) launches a child of argv = ["sh", "-c", cmd]
        ::firebuild::ExecedProcessParameters expected_child;
        // FIXME what if !has_cmd() ?
        expected_child.set_sh_c_command(ic_msg.popen().cmd());
        proc->add_expected_child(expected_child);
      } else if (ic_msg.has_popen_parent()) {
        // FIXME(egmont) Connect pipe's end with child
      } else if (ic_msg.has_popen_failed()) {
        ::firebuild::ExecedProcessParameters expected_child;
        expected_child.set_sh_c_command(ic_msg.popen_failed().cmd());
        // FIXME what if !has_cmd() ?
        if (!proc->remove_expected_child(expected_child)) {
          firebuild::fb_error("Failed to remove \"" + ic_msg.popen_failed().cmd()
                              + "\" from expected_children after a failed popen");
        }
      } else if (ic_msg.has_posix_spawn()) {
        ::firebuild::ExecedProcessParameters expected_child;
        for (const auto &arg : ic_msg.posix_spawn().arg()) {
          expected_child.argv().push_back(arg);
        }
        proc->add_expected_child(expected_child);
      } else if (ic_msg.has_posix_spawn_parent()) {
        // FIXME(egmont)
      } else if (ic_msg.has_posix_spawn_failed()) {
        ::firebuild::ExecedProcessParameters expected_child;
        for (const auto &arg : ic_msg.posix_spawn_failed().arg()) {
          expected_child.argv().push_back(arg);
        }
        if (!proc->remove_expected_child(expected_child)) {
          firebuild::fb_error("Failed to remove " + to_string(expected_child)
                              + " from expected_children after a failed posix_spawn[p]");
        }
      } else if (ic_msg.has_execv()) {
        proc->update_rusage(ic_msg.execv().utime_u(),
                            ic_msg.execv().stime_u());
        // FIXME(rbalint) save execv parameters
      } else if (ic_msg.has_open()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.open());
      } else if (ic_msg.has_close()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.close());
      } else if (ic_msg.has_pipe2()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.pipe2());
      } else if (ic_msg.has_dup3()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.dup3());
      } else if (ic_msg.has_dup()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.dup());
      } else if (ic_msg.has_fcntl()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.fcntl());
      } else if (ic_msg.has_chdir()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.chdir());
      }
    } catch (std::out_of_range&) {
      FB_DEBUG(1, "Ignoring message on fd: " + std::to_string(fd_conn) +
               std::string(", process probably exited already."));
    }
  } else if (ic_msg.has_gen_call()) {
  }

  if (ic_msg.has_ack_num()) {
    ack_msg(fd_conn, ic_msg.ack_num());
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
  if (-1 == d3) {
    perror("open");
    firebuild::fb_error("Opening file " + (datadir + "/" + d3_filename) +
                        " failed.");
    firebuild::fb_error("Can not write build report.");
    return;
  }

  FILE* src_file = fopen((datadir + "/" + html_orig_filename).c_str(), "r");
  if (NULL == src_file) {
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
    if (NULL == dot) {
      perror("fopen");
      firebuild::fb_error("Failed to open dot file for writing profile graph.");
    }
    proc_tree->export_profile2dot(dot);
    fclose(dot);
  }

  system((dot_cmd + " -Tsvg -o" + dir + "/" + svg_filename + " " + dir
          + "/" + dot_filename).c_str());

  FILE* dst_file = fopen(html_filename.c_str(), "w");
  int ret = (NULL == dst_file)?-1:0;
  while ((ret != -1)) {
    char* line = NULL;
    size_t zero = 0;
    if (-1 == getline(&line, &zero, src_file)) {
      // finished reading file
      if (!feof(src_file)) {
        perror("getline");
        firebuild::fb_error("Reading from report template failed.");
      }
      free(line);
      break;
    }
    if (NULL != strstr(line, d3_filename)) {
      fprintf(dst_file, "<script type=\"text/javascript\">\n");

      fflush(dst_file);
      ret = sendfile_full(fileno(dst_file), d3);
      fsync(fileno(dst_file));
      fprintf(dst_file, "    </script>\n");
    } else if (NULL != strstr(line, tree_filename)) {
      fprintf(dst_file, "<script type=\"text/javascript\">\n");
      proc_tree->export2js(dst_file);
      fprintf(dst_file, "    </script>\n");
    } else if (NULL != strstr(line, svg_filename)) {
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
    snprintf(local.sun_path, sizeof(local.sun_path), "%s%d", fb_conn_string.c_str(), i);

    unlink(local.sun_path);
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
  for (auto const listener : fb_listener_pool) {
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
  for (auto const listener : fb_listener_pool) {
    if (fd == listener) {
      return true;
    }
  }
  return false;
}

int main(const int argc, char *argv[]) {
  char *config_file = NULL;
  int c;

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
      {"insert-trace-markers", no_argument,       0, 'i' },
      {0,                                0,       0,  0  }
    };

    c = getopt_long(argc, argv, "c:d:r::hi",
                    long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'c':
      config_file = optarg;
      break;

    case 'd':
      firebuild::debug_level = atoi(optarg);
      if ((firebuild::debug_level < 0) || (firebuild::debug_level > 4)) {
        usage();
        exit(EXIT_FAILURE);
      }
      break;

    case 'h':
      usage();
      exit(EXIT_SUCCESS);
      // break;

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

  parse_cfg_file(config_file);

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
  cache = new firebuild::Cache(cache_dir + "/blobs");
  multi_cache = new firebuild::MultiCache(cache_dir + "/pbs");

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
    fb_conn_string = std::string(fb_tmp_dir) + "/socket.";
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

    firebuild::msg::InterceptorMsg ic_msg;
    firebuild::msg::SupervisorMsg sv_msg;

    bool child_exited = false;

    uid_t euid = geteuid();

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // add the listener and and fd listening for child's death to the
    // master set
    for (auto const listener : fb_listener_pool) {
      FD_SET(listener, &master);
      fdmax = (listener > fdmax)?listener:fdmax;
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
            ssize_t nbytes;

            if ((nbytes = firebuild::fb_recv_msg(&ic_msg, i)) <= 0) {
              // got error or connection closed by client
              if (nbytes == 0) {
                // connection closed
                FB_DEBUG(2, "socket " + std::to_string(i) +
                         std::string(" hung up"));
              } else {
                perror("recv");
              }
              proc_tree->finished(i);
              close(i);  // bye!
              FD_CLR(i, &master);  // remove from master set
            } else {
              if (firebuild::debug_level >= 2) {
                FB_DEBUG(2, "fd " + std::to_string(i) + std::string(": "));
                google::protobuf::TextFormat::Print(ic_msg, error_fos);
                error_fos->Flush();
              }
              proc_ic_msg(ic_msg, i);
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
    for (int i = 0; NULL != (env_str = env_exec[i]); i++) {
      free(env_str);
    }
    free(env_exec);
  }

  close_listeners();
  for (size_t i = 0; i < (sizeof(fb_listener_pool) / sizeof(fb_listener_pool[0])); i++) {
    unlink((fb_conn_string + std::to_string(i)).c_str());
  }
  rmdir(fb_tmp_dir);

  delete(error_fos);
  fclose(sigchld_stream);
  free(fb_tmp_dir);
  delete(proc_tree);
  delete(cfg);

  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();

  exit(child_ret);
}


namespace firebuild {

/** wrapper for write() retrying on recoverable errors*/
ssize_t fb_write_buf(const int fd, const void * buf, const size_t count) {
  FB_IO_OP_BUF(send, fd, buf, count, MSG_NOSIGNAL, {});
}

/** wrapper for read() retrying on recoverable errors*/
ssize_t fb_read_buf(const int fd, void *buf, const size_t count) {
  FB_IO_OP_BUF(recv, fd, buf, count, 0, {});
}

/** Print error message */
extern void fb_error(const std::string &msg) {
  std::cerr << "FireBuild error: " << msg << std::endl;
}

/** Print debug message if debug level is at least lvl */
extern void fb_debug(const std::string &msg) {
  std::cerr << msg << std::endl;
}

int debug_level = 0;

}  // namespace firebuild
