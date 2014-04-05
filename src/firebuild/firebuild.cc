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

#include "firebuild/firebuild_common.h"
#include "firebuild/Debug.h"
#include "firebuild/ProcessFactory.h"
#include "firebuild/ProcessTree.h"
#include "firebuild/ProcessPBAdaptor.h"
#include "firebuild/fb-messages.pb.h"

namespace {

static char global_cfg[] = "/etc/firebuildrc";

static char datadir[] = FIREBUILD_DATADIR;

static char *fb_conn_string;
static int sigchld_fds[2];
static int child_pid, child_ret = 1;
static google::protobuf::io::FileOutputStream * error_fos;
static bool insert_trace_markers = false;
static bool generate_report = false;
static char *report_file = const_cast<char*>("firebuild-build-report.html");
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
         "   -i --insert-trace-markers perform open(\"/firebuild-intercept-begin\", 0)\n"
         "                             and open(\"/firebuild-intercept-end\", 0) calls\n"
         "                             to let users find unintercepted calls using\n"
         "                             strace or ltrace\n"
         "Exit status:\n"
         " exit status of the BUILD COMMAND\n"
         " 1  in case of failure\n");
}

/** Parse configuration file */
static void parse_cfg_file(const char * const custom_cfg_file) {
  const char * cfg_file = custom_cfg_file;
  if (cfg_file == NULL) {
    char * homedir = getenv("HOME");
    int cfg_fd;
    if ((homedir != NULL ) &&
        (-1 != (cfg_fd = open(std::string(homedir +
                                          std::string("/.firebuildrc")).c_str(),
                              O_RDONLY)))) {
      // fall back to private config file
      cfg_file = std::string(homedir + std::string("/.firebuildrc")).c_str();
      close(cfg_fd);
    } else {
      cfg_fd = open(global_cfg, O_RDONLY);
      if (cfg_fd != -1) {
        // fall back to global config file
        cfg_file = global_cfg;
        close(cfg_fd);
      }
    }
  }
  try {
    cfg->readFile(cfg_file);
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
  env_v.push_back("FB_SOCKET=" + std::string(fb_conn_string));
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
    setvbuf(fdopen(sigchld_fds[1], "w"), NULL, _IONBF, 0);
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
 * @return fd_conn can be kept open
 */
bool proc_ic_msg(const firebuild::msg::InterceptorMsg &ic_msg,
                 const int fd_conn) {
  bool ret = true;
  if (ic_msg.has_scproc_query()) {
    firebuild::msg::SupervisorMsg sv_msg;
    auto scproc_resp = sv_msg.mutable_scproc_resp();
    /* record new process */
    auto proc =
        firebuild::ProcessFactory::getExecedProcess(ic_msg.scproc_query());
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
    try {
      pproc = proc_tree->pid2proc().at(ic_msg.fork_child().ppid());
    } catch (const std::out_of_range& oor) {
      // If parent is missing, FireBuild missed process
      // that can happen due to the missing process(es) being statically built
      std::cerr << "TODO(rbalint) handle: Process without known parent\n";
    }
      /* record new process */
    auto proc =
        firebuild::ProcessFactory::getForkedProcess(ic_msg.fork_child(), pproc);
    proc_tree->insert(proc, fd_conn);
  } else if (ic_msg.has_execvfailed()) {
    auto *proc = proc_tree->pid2proc().at(ic_msg.execvfailed().pid());
    proc_tree->sock2proc()[fd_conn] = proc;
  } else if (ic_msg.has_proc()) {
  } else if (ic_msg.has_exit() ||
             ic_msg.has_execv() ||
             ic_msg.has_open() ||
             ic_msg.has_close() ||
             ic_msg.has_chdir() ||
             ic_msg.has_fdopendir() ||
             ic_msg.has_opendir()) {
    try {
      ::firebuild::Process *proc = proc_tree->sock2proc().at(fd_conn);
      if (ic_msg.has_exit()) {
        proc->exit_result(ic_msg.exit().exit_status(),
                          ic_msg.exit().utime_m(),
                          ic_msg.exit().stime_m());
        proc_tree->exit(proc, fd_conn);
        ret = false;
      } else if (ic_msg.has_execv()) {
        proc->update_rusage(ic_msg.execv().utime_m(),
                            ic_msg.execv().stime_m());
      } else if (ic_msg.has_open()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.open());
      } else if (ic_msg.has_close()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.close());
      } else if (ic_msg.has_chdir()) {
        ::firebuild::ProcessPBAdaptor::msg(proc, ic_msg.chdir());
      }
    } catch (std::out_of_range) {
      FB_DEBUG(1, "Ignoring message on fd: " + std::to_string(fd_conn) +
               std::string(", process probably exited already."));
    }
    ack_msg(fd_conn, ic_msg.ack_num());
  } else if (ic_msg.has_gen_call()) {
  }

  return ret;
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
  const char d3_filename[] = "d3.v3.min.js";
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

}  // namespace

int main(const int argc, char *argv[]) {
  char *config_file = NULL;
  int i, c;

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

  // Verify that the version of the ProtoBuf library that we linked against is
  // compatible with the version of the headers we compiled against.
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  error_fos = new google::protobuf::io::FileOutputStream(STDERR_FILENO);
  {
    fb_conn_string = tempnam(NULL, "firebuild");
  }
  auto env_exec = get_sanitized_env();

  init_signal_handlers();

  if (pipe(sigchld_fds) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  // run command and handle interceptor messages
  {
    int listener;     // listening socket descriptor
    if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      perror("socket");
      exit(EXIT_FAILURE);
    }

    struct sockaddr_un local;
    local.sun_family = AF_UNIX;
    strncpy(local.sun_path, fb_conn_string, sizeof(local.sun_path));
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

    if ((child_pid = fork()) == 0) {
      // intercepted process
      char* argv_exec[argc - optind + 1];

      // we don't need those
      close(sigchld_fds[0]);
      close(sigchld_fds[1]);
      close(listener);
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
      int fdmax;        // maximum file descriptor number

      fd_set master;    // master file descriptor list
      fd_set read_fds;  // temp file descriptor list for select()

      firebuild::msg::InterceptorMsg ic_msg;
      firebuild::msg::SupervisorMsg sv_msg;

      bool child_exited = false;

      uid_t euid = geteuid();

      FD_ZERO(&master);    // clear the master and temp sets
      FD_ZERO(&read_fds);

      // add the listener and and fd listening for child's deeath to the
      // master set
      FD_SET(listener, &master);
      FD_SET(sigchld_fds[0], &master);

      // keep track of the biggest file descriptor
      fdmax = listener;  // so far, it's this one
      // main loop for processing interceptor messages
      for (;;) {
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
            if (i == listener) {
              // handle new connections
              struct sockaddr_un remote;
              socklen_t addrlen = sizeof(remote);

              // newly accept()ed socket descriptor
              int newfd = accept(listener,
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
                  // TODO(rbalint) handle process exit
                  FB_DEBUG(2, "socket " + std::to_string(i) +
                           std::string(" hung up"));
                } else {
                  perror("recv");
                }
                close(i);  // bye!
                FD_CLR(i, &master);  // remove from master set
              } else {
                if (firebuild::debug_level >= 2) {
                  FB_DEBUG(2, "fd " + std::to_string(i) + std::string(": "));
                  google::protobuf::TextFormat::Print(ic_msg, error_fos);
                  error_fos->Flush();
                }
                if (!proc_ic_msg(ic_msg, i)) {
                  fsync(i);
                  close(i);  // bye!
                  FD_CLR(i, &master);  // remove from master set
                }
              }
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
    for (i = 0; NULL != (env_str = env_exec[i]); i++) {
      free(env_str);
    }
    free(env_exec);

    unlink(fb_conn_string);
  }

  delete(error_fos);
  free(fb_conn_string);

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
