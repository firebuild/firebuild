
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
#include <fstream>
#include <cerrno>
#include <cstdio>
#include <stdexcept>

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <libconfig.h++>

#include "firebuild_common.h"
#include "ProcessTree.h"
#include "ProcessPBAdaptor.h"
#include "fb-messages.pb.h"

namespace {

static char global_cfg[] = "/etc/firebuildrc";

static char datadir[] = FIREBUILD_DATADIR;

static char *fb_conn_string;
static int sigchld_fds[2];
static int child_pid, child_ret = 1;
static google::protobuf::io::FileOutputStream * error_fos;
static int debug_level = 0;
static bool insert_trace_markers = false;
static bool generate_report = false;
static char *report_file = (char*)"firebuild-build-report.html";
static firebuild::ProcessTree *proc_tree;

/** global configuration */
libconfig::Config * cfg;

static void usage()
{
  std::cout << "Usage: firebuild [OPTIONS] <BUILD COMMAND>" << std::endl;
  std::cout << "Execute BUILD COMMAND with FireBuild™ instrumentation" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "Mandatory arguments to long options are mandatory for short options too." << std::endl;
  std::cout << "   -c --config-file=FILE     use FILE as configuration file" << std::endl;
  std::cout << "   -d --debug-level=N        set debugging level to N (0-3, default is 0)" << std::endl;
  std::cout << "   -r --generate-report[=HTML] generate a report on the build command execution." << std::endl;
  std::cout << "                             the report's filename can be specified " << std::endl;
  std::cout << "                             (firebuild-build-report.html by default). " << std::endl;
  std::cout << "   -h --help                 show this help" << std::endl;
  std::cout << "   -i --insert-trace-markers perform open(\"/firebuild-intercept-begin\", 0)" << std::endl;
  std::cout << "                             and open(\"/firebuild-intercept-end\", 0) calls" << std::endl;
  std::cout << "                             to let users find unintercepted calls using" << std::endl;
  std::cout << "                             strace or ltrace" << std::endl;
  std::cout << "Exit status:" << std::endl;
  std::cout << " exit status of the BUILD COMMAND" << std::endl;
  std::cout << " 1  in case of failure" << std::endl;
}

/** Parse configuration file */
static void
parse_cfg_file(const char * const custom_cfg_file)
{
  const char * cfg_file = custom_cfg_file;
  if (cfg_file == NULL) {
    char * homedir = getenv("HOME");
    int cfg_fd;
    if ((homedir != NULL ) && (-1 != (cfg_fd = open(std::string(homedir + std::string("/.firebuildrc")).c_str(), O_RDONLY)))) {
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
  try
    {
      cfg->readFile(cfg_file);
    }
  catch(const libconfig::FileIOException &fioex)
    {
      std::cerr << "Could not read configuration file " << cfg_file << std::endl;
      exit(EXIT_FAILURE);
    }
  catch(const libconfig::ParseException &pex)
    {
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
static char** get_sanitized_env()
{
  const libconfig::Setting& root = cfg->getRoot();

  firebuild::fb_debug(1, "Passing through environment variables:");

  const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
  std::vector<std::string> env_v;
  for (int i = 0; i < pass_through.getLength(); i++) {
    char * got_env = getenv(pass_through[i].c_str());
    if (NULL  != got_env) {
      env_v.push_back(pass_through[i].c_str() + std::string("=") + std::string(got_env));
      firebuild::fb_debug(1, " " + env_v.back());
    }
  }
  firebuild::fb_debug(1, "");

  firebuild::fb_debug(1, "Setting preset environment variables:");
  const libconfig::Setting& preset = root["env_vars"]["preset"];
  for (int i = 0; i < preset.getLength(); i++) {
    env_v.push_back(preset[i]);
    firebuild::fb_debug(1, " " + env_v.back());
  }
  env_v.push_back("FB_SOCKET=" + std::string(fb_conn_string));
  firebuild::fb_debug(1, " " + env_v.back());

  firebuild::fb_debug(1, "");

  if (insert_trace_markers) {
    env_v.push_back("FB_INSERT_TRACE_MARKERS=1");
  }

  char ** ret_env = static_cast<char**>(malloc(sizeof(char*) * (env_v.size() + 1)));

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
static void
sigchld_handler (const int /*sig */)
{
  char buf[] = {0};
  int status = 0;

  waitpid(child_pid, &status, WNOHANG);
  if (WIFEXITED(status)) {
    child_ret = WEXITSTATUS(status);
    setvbuf(fdopen(sigchld_fds[1],"w"), NULL, _IONBF, 0);
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
static void
init_signal_handlers(void)
{
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
void
ack_msg (const int conn)
{
  firebuild::msg::SupervisorMsg sv_msg;
  sv_msg.set_ack(true);
  firebuild::fb_send_msg(sv_msg, conn);
}

/**
 * Process message coming from interceptor
 * @param fb_conn file desctiptor of the connection
 * @return fd_conn can be kept open
 */
bool proc_ic_msg(const firebuild::msg::InterceptorMsg &ic_msg, const int fd_conn) {
  bool ret = true;
  if (ic_msg.has_scproc_query()) {
    firebuild::msg::SupervisorMsg sv_msg;
    auto scproc_resp = sv_msg.mutable_scproc_resp();
    /* record new process */
    auto proc = new ::firebuild::ExecedProcess(ic_msg.scproc_query());
    proc_tree->insert(*proc, fd_conn);
    // TODO look up stored result
    if (false /* can shortcut*/) {
      scproc_resp->set_shortcut(true);
      scproc_resp->set_exit_status(0);
    } else {
      scproc_resp->set_shortcut(false);
      if (debug_level != 0) {
        scproc_resp->set_debug_level(debug_level);
      }
    }
    firebuild::fb_send_msg(sv_msg, fd_conn);
  } else if (ic_msg.has_fork_child()) {
    ::firebuild::ForkedProcess* proc;
    /* record new process */
    proc = new ::firebuild::ForkedProcess (ic_msg.fork_child());
    proc_tree->insert(*proc, fd_conn);
  } else if (ic_msg.has_execvfailed()) {
    auto *proc = proc_tree->pid2proc().at(ic_msg.execvfailed().pid());
    proc_tree->sock2proc()[fd_conn] = proc;
  } else if (ic_msg.has_proc()) {
  } else if (ic_msg.has_exit() ||
             ic_msg.has_execv() ||
             ic_msg.has_open() ||
             ic_msg.has_close() ||
             ic_msg.has_fdopendir() ||
             ic_msg.has_opendir()) {
    try {
      ::firebuild::Process *proc = proc_tree->sock2proc().at(fd_conn);
      if (ic_msg.has_exit()) {
        proc->exit_result(ic_msg.exit().exit_status(),
                          ic_msg.exit().utime_m(),
                          ic_msg.exit().stime_m());
        proc_tree->exit(*proc, fd_conn);
        ret = false;
      } else if (ic_msg.has_execv()) {
        proc->update_rusage(ic_msg.execv().utime_m(),
                            ic_msg.execv().stime_m());
      } else if (ic_msg.has_open()) {
        ::firebuild::ProcessPBAdaptor::msg(*proc, ic_msg.open());
      } else if (ic_msg.has_close()) {
        ::firebuild::ProcessPBAdaptor::msg(*proc, ic_msg.close());
      }
    } catch (std::out_of_range) {
      firebuild::fb_debug(1, "Ignoring message on fd: " + fd_conn +
                          std::string(", process probably exited already."));
    }
    ack_msg(fd_conn);
  } else if (ic_msg.has_gen_call()) {
  }

  return ret;
}

/**
 * Write report to specified file
 *
 * @param html_filename report file to be written
 * @param datadir report template's location
 * TODO error handling
 */
static void write_report(const std::string &html_filename, const std::string &datadir){
  const char dot_filename[] = "firebuild-profile.dot";
  const char svg_filename[] = "firebuild-profile.svg";
  const char d3_filename[] = "d3.v3.min.js";
  const char tree_filename[] = "firebuild-process-tree.js";
  const char html_orig_filename[] = "build-report.html";
  const std::string dot_cmd = "dot";
  std::ifstream d3(datadir + "/" + d3_filename);
  std::ifstream src(datadir + "/" + html_orig_filename);

  // dirname may modify its parameter thus we provide a writable char std::string
  char *html_filename_tmp = new char [html_filename.size() + 1] ;
  strcpy(html_filename_tmp, html_filename.c_str());
  std::string dir = dirname(html_filename_tmp);
  delete[] html_filename_tmp;

  // export profile
  {
    std::fstream dot;
    dot.open (dir + "/" + dot_filename, std::fstream::out);
    proc_tree->export_profile2dot(dot);
    dot.close();
  }

  system((dot_cmd + " -Tsvg -o" + dir + "/" + svg_filename + " " + dir
          + "/" + dot_filename).c_str());

  std::ofstream dst(html_filename);
  while (src.good() && dst.good()) {
    std::string line;
    getline(src, line);
    if (NULL != strstr(line.c_str(), d3_filename)) {
      dst << "<script type=\"text/javascript\">" << std::endl;
      dst << d3.rdbuf();
      dst << "    </script>" << std::endl;
    } else if (NULL != strstr(line.c_str(), tree_filename)) {
      dst << "    <script type=\"text/javascript\">" << std::endl;
      proc_tree->export2js(dst);
      dst << "    </script>" << std::endl;
    } else if (NULL != strstr(line.c_str(), svg_filename)) {
      std::ifstream svg(dir + "/" + svg_filename);
      dst << svg.rdbuf();
      svg.close();
    } else {
      dst << line << std::endl;
    }
  }
  d3.close();
  src.close();
}

} // namespace

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
      {"config-file",  required_argument, 0,  'c' },
      {"debug-level",  required_argument, 0,  'd' },
      {"generate-report",  optional_argument, 0,  'r' },
      {"help",         no_argument,       0,  'h' },
      {"insert-trace-markers", no_argument, 0,'i' },
      {0,         0,                 0,  0 }
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
      debug_level = atoi(optarg);
      if ((debug_level < 0) || (debug_level > 3)) {
        usage();
        exit(EXIT_FAILURE);
      }
      break;

    case 'h':
      usage();
      exit(EXIT_SUCCESS);
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

      // add the listener and and fd listening for child's deeath to the master set
      FD_SET(listener, &master);
      FD_SET(sigchld_fds[0], &master);

      // keep track of the biggest file descriptor
      fdmax = listener; // so far, it's this one
      // main loop for processing interceptor messages
      for(;;) {
        if (child_exited) {
          break;
        }
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
          if (errno != EINTR) {
            perror("select");
            exit(1);
          } else {
            break;
          }
        }

        for(i = 0; i <= fdmax; i++) {
          if (FD_ISSET(i, &read_fds)) { // we got one!!
            //fd_handled = true;
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
                          "Unauthorized connection from pid %d, uid %d, gid %d\n",
                          creds.pid, creds.uid, creds.gid);
                  close(newfd);
                } else {
                  FD_SET(newfd, &master); // add to master set
                  if (newfd > fdmax) {    // keep track of the max
                    fdmax = newfd;
                  }
                }
                // TODO debug
              }
            } else if (i == sigchld_fds[0]) {
              // Our child has exited.
              // Process remaining messages, then we are done.
              child_exited = true;
              continue;
            } else {
              // handle data from a client
              ssize_t nbytes;

              if ((nbytes = firebuild::fb_recv_msg(ic_msg, i)) <= 0) {
                // got error or connection closed by client
                if (nbytes == 0) {
                  // connection closed
                  // TODO handle process exit
                  firebuild::fb_debug(2, "socket " + i + std::string(" hung up"));
                } else {
                  perror("recv");
                }
                close(i); // bye!
                FD_CLR(i, &master); // remove from master set
              } else {
                firebuild::fb_debug(2, "fd " + i + std::string(": "));
                google::protobuf::TextFormat::Print(ic_msg, error_fos);
                error_fos->Flush();

                if (!proc_ic_msg(ic_msg, i)) {
                  fsync(i);
                  close(i); // bye!
                  FD_CLR(i, &master); // remove from master set
                }
              }
            }
          }
        }
      }
    }
  }

  if (!proc_tree->root()) {
    std::cerr << "ERROR: Could not collect any information about the build process" << std::endl;
    child_ret = EXIT_FAILURE;
  } else {
    // postprocess process tree
    proc_tree->sum_rusage_recurse(*proc_tree->root());

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
ssize_t fb_write_buf(const int fd, const void * buf, const size_t count)
{
  FB_IO_OP_BUF(send, fd, buf, count, MSG_NOSIGNAL, {});
}

/** wrapper for read() retrying on recoverable errors*/
ssize_t fb_read_buf(const int fd, void *buf, const size_t count)
{
  FB_IO_OP_BUF(recv, fd, buf, count, 0, {});
}

/** Print error message */
extern void fb_error(const std::string &msg)
{
  std::cerr << msg << std::endl;
}

/** Print debug message if debug level is at least lvl */
extern void fb_debug(const int lvl, const std::string &msg)
{
  if (debug_level >= lvl) {
    std::cerr << msg << std::endl;
  }
}

} // namespace firebuild
