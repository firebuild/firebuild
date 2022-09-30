/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include <signal.h>
#include <getopt.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>

#include <string>
#include <stdexcept>
#include <libconfig.h++>

#include "firebuild/debug.h"
#include "firebuild/config.h"
#include "firebuild/connection_context.h"
#include "firebuild/epoll.h"
#include "firebuild/file_name.h"
#include "firebuild/message_processor.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/process.h"
#include "firebuild/process_debug_suppressor.h"
#include "firebuild/process_tree.h"
#include "firebuild/report.h"

/** global configuration */
libconfig::Config * cfg;
bool generate_report = false;

firebuild::Epoll *epoll = nullptr;

int sigchild_selfpipe[2];

namespace {

static char *fb_tmp_dir;
static char *fb_conn_string;

int listener;

static int child_pid, child_ret = 1;

static bool insert_trace_markers = false;
static const char *report_file = "firebuild-build-report.html";

/** only if debugging "time" */
struct timespec start_time;

static void usage() {
  printf("Usage: firebuild [OPTIONS] <BUILD COMMAND>\n"
         "Execute BUILD COMMAND with Firebuild instrumentation\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "   -c --config-file=FILE     Use FILE as configuration file.\n"
         "                             If not specified, load .firebuild.conf, ~/.firebuild.conf,\n"
         "                             $XDG_CONFIG_HOME/firebuild/firebuild.conf or\n"
         "                             /etc/firebuild.conf in that order.\n"
         "   -C --directory=DIR        change directory before running the command\n"
         "   -d --debug-flags=list     comma separated list of debug flags,\n"
         "                             \"-d help\" to get a list.\n"
         "   -D --debug-filter=list    comma separated list of commands to debug.\n"
         "                             Debug messages related to processes which are not listed\n"
         "                             are suppressed.\n"
         "   -g --gc                   Garbage collect the cache.\n"
         "                             Keeps debugging entries related to kept files when used\n"
         "                             together with \"--debug cache\".\n"
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

static void save_child_status(pid_t pid, int status, int * ret, bool orphan) {
  TRACK(firebuild::FB_DEBUG_PROC, "pid=%d, status=%d, orphan=%s", pid, status, D(orphan));

  if (WIFEXITED(status)) {
    *ret = WEXITSTATUS(status);
    firebuild::Process* proc = firebuild::proc_tree->pid2proc(pid);
    if (proc && proc->fork_point()) {
      proc->fork_point()->set_exit_status(*ret);
    }
    FB_DEBUG(firebuild::FB_DEBUG_COMM, std::string(orphan ? "orphan" : "child")
             + " process exited with status " + std::to_string(*ret) + ". ("
             + d(firebuild::proc_tree->pid2proc(pid)) + ")");
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
      firebuild::Process* proc = firebuild::proc_tree->pid2proc(child_pid);
      assert(proc);
      firebuild::ProcessDebugSuppressor debug_suppressor(proc);
      save_child_status(waitpid_ret, status, &child_ret, false);
      proc->set_been_waited_for();
    } else if (waitpid_ret > 0) {
      /* This is an orphan process. Its fork parent quit without wait()-ing for it
       * and as a subreaper the supervisor received the SIGCHLD for it. */
      firebuild::Process* proc = firebuild::proc_tree->pid2proc(waitpid_ret);
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
    auto conn_ctx = new firebuild::ConnectionContext(fd);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    epoll->add_fd(fd, EPOLLIN, firebuild::MessageProcessor::ic_conn_readcb, conn_ctx);
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
  bool gc_only = false;
  /* init global data */
  cfg = new libconfig::Config();

  /* parse options */
  setenv("POSIXLY_CORRECT", "1", true);
  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      {"config-file",          required_argument, 0, 'c' },
      {"gc",                   no_argument,       0, 'g' },
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

    c = getopt_long(argc, argv, "c:C:d:D:r::o:ghi",
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

    case 'g':
      gc_only = true;
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
  if (optind >= argc && !gc_only) {
    usage();
    exit(EXIT_FAILURE);
  }

  if (FB_DEBUGGING(firebuild::FB_DEBUG_TIME)) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
  }

  firebuild::read_config(cfg, config_file, config_strings);

  /* Initialize the cache */
  firebuild::ExecedProcessCacher::init(cfg);

  if (gc_only) {
    firebuild::execed_process_cacher->gc();
    exit(0);
  }
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
  auto env_exec = firebuild::get_sanitized_env(cfg, fb_conn_string, insert_trace_markers);

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
    firebuild::proc_tree = new firebuild::ProcessTree();

    /* Add a ForkedProcess for the supervisor's forked child we never directly saw. */
    firebuild::proc_tree->insert_root(child_pid, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

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
    firebuild::proc_tree->FinishInheritedFdPipes();
    /* Close the self-pipe */
    close(sigchild_selfpipe[0]);
    close(sigchild_selfpipe[1]);
  }

  if (firebuild::debug_filter) {
    firebuild::debug_suppressed = false;
  }

  if (!firebuild::proc_tree->root()) {
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
      firebuild::Report::write(report_file, datadir);
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
    delete(firebuild::proc_tree);
    delete(cfg);
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
  }

  exit(child_ret);
}
