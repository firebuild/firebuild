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

#include "firebuild/firebuild.h"

#ifdef __APPLE__
#include <crt_externs.h>
#endif
#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>

#include <iostream>
#include <list>
#include <string>
#include <stdexcept>
#include <libconfig.h++>

#include "firebuild/debug.h"
#include "firebuild/sigchild_callback.h"
#include "firebuild/config.h"
#include "firebuild/connection_context.h"
#include "firebuild/epoll.h"
#include "firebuild/file_name.h"
#include "firebuild/options.h"
#include "firebuild/message_processor.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/process.h"
#include "firebuild/process_tree.h"
#include "firebuild/report.h"
#include "firebuild/utils.h"

int sigchild_selfpipe[2];

int listener;

int child_pid, child_ret = 1;

namespace {

static char *fb_tmp_dir;
static char *fb_conn_string;

static bool stats_saved = false;

/** only if debugging "time" */
struct timespec start_time;

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
    if (firebuild::epoll->is_added_fd(fd)) {
      /* This happens very rarely. Just when the file descriptor has been closed by the other end,
       * the epoll loop did not process this event yet, but the file descriptor got reused for the
       * new connection. */
      fd = firebuild::epoll->remap_to_not_added_fd(fd);
    }
    firebuild::bump_fd_age(fd);
    auto conn_ctx = new firebuild::ConnectionContext(fd);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    firebuild::epoll->add_fd(fd, EPOLLIN, firebuild::MessageProcessor::ic_conn_readcb, conn_ctx);
  }
}

static void sigterm_handler(int signum) {
  if (!stats_saved) {
    firebuild::execed_process_cacher->read_update_save_stats_and_bytes();
    stats_saved = true;
  }
  std::cerr << "FIREBUILD: Received signal " + std::to_string(signum) + ", exiting." << std::endl;
  exit(EXIT_FAILURE);
}

static bool running_under_valgrind() {
  const char *v = getenv(LD_PRELOAD);
  if (v == NULL) {
    return false;
  } else {
    return (strstr(v, "/valgrind/") != NULL || strstr(v, "/vgpreload") != NULL);
  }
}


int main(const int argc, char *argv[]) {
  /* init global data */
  firebuild::cfg = new libconfig::Config();
  /* parse options */
  setenv("POSIXLY_CORRECT", "1", true);
  firebuild::Options::parse(argc, argv);

  if (FB_DEBUGGING(firebuild::FB_DEBUG_TIME)) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
  }

  firebuild::read_config(firebuild::cfg, firebuild::Options::config_file(),
                         firebuild::Options::config_strings());

  /* Initialize the cache */
  firebuild::ExecedProcessCacher::init(firebuild::cfg);

  if (firebuild::Options::reset_stats()) {
    firebuild::execed_process_cacher->reset_stored_stats();
  }
  if (!firebuild::Options::build_cmd()) {
    if (firebuild::Options::do_gc()) {
      firebuild::execed_process_cacher->gc();
      firebuild::execed_process_cacher->update_stored_bytes();
      /* Store GC runs, too. */
      firebuild::execed_process_cacher->update_stored_stats();
    }
    if (firebuild::Options::print_stats()) {
      if (!firebuild::Options::do_gc()) {
        firebuild::execed_process_cacher->add_stored_stats();
      }
      firebuild::execed_process_cacher->print_stats(firebuild::FB_SHOW_STATS_STORED);
    }
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
  auto env_exec = firebuild::get_sanitized_env(firebuild::cfg, fb_conn_string,
                                               firebuild::Options::insert_trace_markers());

  /* Set up sigchild handler */
  if (fb_pipe2(sigchild_selfpipe, O_CLOEXEC | O_NONBLOCK) != 0) {
    firebuild::fb_perror("pipe");
    exit(EXIT_FAILURE);
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchild_handler;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);
  sa.sa_handler = sigterm_handler;
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  /* Configure epoll */
  firebuild::epoll = new firebuild::Epoll();

  /* Open listener socket before forking child to always let the child connect */
  listener = create_listener();
  firebuild::epoll->add_fd(listener, EPOLLIN, accept_ic_conn, NULL);

#ifdef __linux__
  /* Collect orphan children */
  prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif

  firebuild::check_system_setup();

  /* run command and handle interceptor messages */
  if ((child_pid = fork()) == 0) {
    /* intercepted process */

    /* we don't need that */
    close(listener);

    if (firebuild::Options::directory() && chdir(firebuild::Options::directory()) != 0) {
      firebuild::fb_perror("chdir");
      exit(EXIT_FAILURE);
    }

#ifdef __APPLE__
    *_NSGetEnviron() = env_exec;
    execvp(firebuild::Options::build_cmd()[0],
           const_cast<char* const*>(firebuild::Options::build_cmd()));
#else
    execvpe(firebuild::Options::build_cmd()[0],
            const_cast<char* const*>(firebuild::Options::build_cmd()), env_exec);
#endif
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

    firebuild::epoll->add_fd(sigchild_selfpipe[0], EPOLLIN, firebuild::sigchild_cb, NULL);

    /* Main loop for processing interceptor messages */
    /* Runs until the only remaining epoll-monitored fd is the sigchild_selfpipe fd. */
    while (firebuild::epoll->fds() > 1) {
      /* This is where the process spends its idle time: waiting for an event over a fd, or a
       * sigchild.
       *
       * If our immediate child exited (rather than some orphan descendant thereof, see
       * prctl(PR_SET_CHILD_SUBREAPER) above) then the handler sigchild_cb() will set listener to
       * -1, that's how we'll break out of this loop. */
      firebuild::epoll->wait();

      /* Process the reported events, if any. */
      firebuild::epoll->process_all_events();

      firebuild::proc_tree->GcProcesses();
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
    struct rusage ru_myslf;
    getrusage(RUSAGE_SELF, &ru_myslf);
    const unsigned int cpu_time_self_ms =
        ru_myslf.ru_utime.tv_sec * 1000 + ru_myslf.ru_utime.tv_usec / 1000 +
        ru_myslf.ru_stime.tv_sec * 1000 + ru_myslf.ru_stime.tv_usec / 1000;
    firebuild::execed_process_cacher->set_self_cpu_time_ms(cpu_time_self_ms);
    /* Print times, including user and sys time separately for firebuild itself and its children.
     * The syntax is similar to bash's "time", although easier to parse (raw seconds in decimal). */
    if (FB_DEBUGGING(firebuild::FB_DEBUG_TIME)) {
      struct timespec end_time, diff_time;
      struct rusage ru_chldr, ru_total;

      clock_gettime(CLOCK_MONOTONIC, &end_time);
      getrusage(RUSAGE_CHILDREN, &ru_chldr);

      timespecsub(&end_time, &start_time, &diff_time);
      timeradd(&ru_myslf.ru_utime, &ru_chldr.ru_utime, &ru_total.ru_utime);
      timeradd(&ru_myslf.ru_stime, &ru_chldr.ru_stime, &ru_total.ru_stime);

      fprintf(stderr, "\nResource usages, in seconds:\n"
              "real           %5" PRItime ".%03ld\n"
#ifdef __APPLE__
              "user firebuild %5" PRItime ".%03d\n"
              "user children  %5" PRItime ".%03d\n"
              "user total     %5" PRItime ".%03d\n"
              "sys  firebuild %5" PRItime ".%03d\n"
              "sys  children  %5" PRItime ".%03d\n"
              "sys  total     %5" PRItime ".%03d\n"
#else
              "user firebuild %5" PRItime ".%03" PRItime "\n"
              "user children  %5" PRItime ".%03" PRItime "\n"
              "user total     %5" PRItime ".%03" PRItime "\n"
              "sys  firebuild %5" PRItime ".%03" PRItime "\n"
              "sys  children  %5" PRItime ".%03" PRItime "\n"
              "sys  total     %5" PRItime ".%03" PRItime "\n"
#endif
              "\n"
              "firebuild's memory usage in MiB:\n"
              "max. res. set  %9.03f\n",
              diff_time.tv_sec, diff_time.tv_nsec / (1000 * 1000),
              ru_myslf.ru_utime.tv_sec, ru_myslf.ru_utime.tv_usec / 1000,
              ru_chldr.ru_utime.tv_sec, ru_chldr.ru_utime.tv_usec / 1000,
              ru_total.ru_utime.tv_sec, ru_total.ru_utime.tv_usec / 1000,
              ru_myslf.ru_stime.tv_sec, ru_myslf.ru_stime.tv_usec / 1000,
              ru_chldr.ru_stime.tv_sec, ru_chldr.ru_stime.tv_usec / 1000,
              ru_total.ru_stime.tv_sec, ru_total.ru_stime.tv_usec / 1000,
              static_cast<double>(ru_myslf.ru_maxrss) / 1024);
    }

    if (firebuild::execed_process_cacher->is_gc_needed()) {
      firebuild::execed_process_cacher->gc();
    }
    if (firebuild::Options::print_stats()) {
      /* Separate stats from other output. */
      fprintf(stdout, "\n");
      firebuild::execed_process_cacher->print_stats(firebuild::FB_SHOW_STATS_CURRENT);
    }
    if (!stats_saved) {
      firebuild::execed_process_cacher->read_update_save_stats_and_bytes();
      stats_saved = true;
    }
    /* show process tree if needed */
    if (firebuild::Options::generate_report()) {
      const std::string datadir(getenv("FIREBUILD_DATA_DIR") ? getenv("FIREBUILD_DATA_DIR")
                                : FIREBUILD_DATADIR);
      firebuild::Report::write(firebuild::Options::report_file(), datadir);
    }
  }

  unlink(fb_conn_string);
  rmdir(fb_tmp_dir);

#ifdef FB_EXTRA_DEBUG
  (void)running_under_valgrind;
  {
#else
  if (running_under_valgrind()) {
#endif
    /* keep Valgrind happy */
    {
      char* env_str;
      for (int i = 0; (env_str = env_exec[i]) != NULL; i++) {
        free(env_str);
      }
      free(env_exec);
    }

    /* No more epoll needed, this also closes all tracked fds */
    delete firebuild::epoll;
    free(fb_conn_string);
    free(fb_tmp_dir);
    delete(firebuild::proc_tree);
    delete(firebuild::cfg);
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
  }

  exit(child_ret);
}
