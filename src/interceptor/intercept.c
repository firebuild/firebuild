/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "interceptor/intercept.h"

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <spawn.h>

#include "interceptor/env.h"
#include "interceptor/ic_file_ops.h"
#include "interceptor/interceptors.h"
#include "common/firebuild_common.h"

static void fb_ic_cleanup() __attribute__((destructor));

/** file fd states */
fd_state ic_fd_states[IC_FD_STATES_SIZE];

/** Global lock for preventing parallel system and popen calls */
pthread_mutex_t ic_system_popen_lock = PTHREAD_MUTEX_INITIALIZER;

/** Global lock for serializing critical interceptor actions */
pthread_mutex_t ic_global_lock = PTHREAD_MUTEX_INITIALIZER;

/** Connection string to supervisor */
char * fb_conn_string = NULL;

/** Connection file descriptor to supervisor */
int fb_sv_conn = -1;

/** pthread_sigmask() if available (libpthread is loaded), otherwise sigprocmask() */
int (*ic_pthread_sigmask)(int, const sigset_t *, sigset_t *);

/** Control for running the initialization exactly once */
pthread_once_t ic_init_control = PTHREAD_ONCE_INIT;

/** Fast check for whether interceptor init has been run */
bool ic_init_done = false;

/** System locations to not ask ACK for when opening them. */
string_array system_locations;
/** System locations to not ask ACK for when opening them, as set in the environment variable. */
char * system_locations_env_str;

bool intercepting_enabled = true;

/**
 * Stored PID
 * When getpid() returns a different value, we missed a fork() :-)
 */
int ic_pid;

__thread const char *thread_intercept_on = NULL;
__thread sig_atomic_t thread_signal_danger_zone_depth = 0;
__thread bool thread_has_global_lock = false;
__thread sig_atomic_t thread_signal_handler_running_depth = 0;
__thread sig_atomic_t thread_libc_nesting_depth = 0;
__thread uint64_t thread_delayed_signals_bitmap = 0;

void (**orig_signal_handlers)(void);

/** Whether to install our wrapper for the given signal. */
bool signal_is_wrappable(int signum) {
  /* Safety check, so that we don't crash if the user passes an invalid value to signal(),
   * sigset() or sigaction(). Just let the original function handle it somehow. */
  if (signum < 1 || signum > SIGRTMAX) {
    return false;
  }

  return true;
}

/** When a signal handler is installed using signal(), sigset(), or sigaction() without the
 *  SA_SIGINFO flag, this wrapper gets installed instead.
 *
 *  See tpl_signal.c for how this wrapper is installed instead of the actual handler.
 *
 *  This wrapper makes sure that the actual signal handler is only executed straight away if the
 *  thread is not inside a "signal danger zone". Otherwise execution is deferred until the danger
 *  zone is left (thread_signal_danger_zone_leave()).
 */
void wrapper_signal_handler_1arg(int signum) {
  char debug_msg[256];

  if (thread_signal_danger_zone_depth > 0) {
    snprintf(debug_msg, sizeof(debug_msg), "signal %d arrived in danger zone, delaying\n", signum);
    insert_debug_msg(debug_msg);
    thread_delayed_signals_bitmap |= (1LL << (signum - 1));
    return;
  }

  thread_signal_handler_running_depth++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-1arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  ((void (*)(int))(*orig_signal_handlers[signum]))(signum);

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-1arg-end %d\n", signum);
  insert_debug_msg(debug_msg);

  thread_signal_handler_running_depth--;
}

/** When a signal handler is installed using sigaction() with the SA_SIGINFO flag,
 *  this wrapper gets installed instead.
 *
 *  See wrapper_signal_handler_3arg() for further details.
 */
void wrapper_signal_handler_3arg(int signum, siginfo_t *info, void *ucontext) {
  char debug_msg[256];

  if (thread_signal_danger_zone_depth > 0) {
    snprintf(debug_msg, sizeof(debug_msg), "signal %d arrived in danger zone, delaying\n", signum);
    insert_debug_msg(debug_msg);
    thread_delayed_signals_bitmap |= (1LL << (signum - 1));
    // FIXME(egmont) stash "info"
    return;
  }

  thread_signal_handler_running_depth++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  // FIXME(egmont) if this is a re-raised signal from thread_raise_delayed_signals()
  // [can this be detected fully reliably, without the slightest race condition?]
  // then replace "info" with the stashed version
  ((void (*)(int, siginfo_t *, void *))(*orig_signal_handlers[signum]))(signum, info, ucontext);

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-end %d\n", signum);
  insert_debug_msg(debug_msg);

  thread_signal_handler_running_depth--;
}

/** Internal helper for thread_signal_danger_zone_leave(), see there for details. */
void thread_raise_delayed_signals() {
  /* Execute the delayed signals, by re-raising them. */
  char debug_msg[256];
  for (int signum = 1; signum <= SIGRTMAX; signum++) {
    if (thread_delayed_signals_bitmap & (1LL << (signum - 1))) {
      snprintf(debug_msg, sizeof(debug_msg), "raising delayed signal %d\n", signum);
      insert_debug_msg(debug_msg);
      thread_delayed_signals_bitmap &= ~(1LL << (signum - 1));
      raise(signum);
    }
  }
}

/** debugging flags */
int32_t debug_flags = 0;

/** Initial LD_LIBRARY_PATH so that we can fix it up if needed */
char *env_ld_library_path = NULL;

/** Insert marker open()-s for strace, ltrace, etc. */
bool insert_trace_markers = false;

/** Next ACK id*/
static uint32_t ack_id = 1;

psfa *psfas = NULL;
int psfas_num = 0;
int psfas_alloc = 0;


/** Insert debug message */
void insert_debug_msg(const char* m) {
  if (insert_trace_markers) {
    int saved_errno = errno;
    char tpl[256] = "/FIREBUILD   ###   ";
    ic_orig_open(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1), 0);
    errno = saved_errno;
  }
}

/** Insert interception begin marker */
void insert_begin_marker(const char* m) {
  if (insert_trace_markers) {
    char tpl[256] = "intercept-begin: ";
    insert_debug_msg(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1));
  }
}

/** Insert interception end marker */
void insert_end_marker(const char* m) {
  if (insert_trace_markers) {
    char tpl[256] = "intercept-end: ";
    insert_debug_msg(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1));
  }
}

/** Get next unique ACK id */
static int get_next_ack_id() {
  return ack_id++;
}

/**
 * Receive a message consisting of an ack_id, followed by either an FBB or the empty message.
 * See common/msg/README_MSG_FRAME.txt for details.
 *
 * If bufp == NULL, the received message has to be an empty one, and this method is
 * async-signal-safe.
 *
 * If bufp != NULL, the received message is stored in a newly allocated buffer which the caller
 * will have to free().
 *
 * It's the caller's responsibility to lock.
 *
 * @param msg the decoded message, or unchanged if the empty message is received
 * @param ack_id_p if non-NULL, store the received ack_id here
 * @param bufp if non-NULL, store the received message here
 * @param fd the communication file descriptor
 * @return the received payload length (0 for empty messages), or -1 on error
 */
static ssize_t fb_recv_msg(uint32_t *ack_id_p, char **bufp, int fd) {
  /* read serialized length and ack_id */
  uint32_t header[2];
  ssize_t ret = fb_read(fd, header, sizeof(header));
  if (ret == -1 || ret == 0) {
    return ret;
  }
  uint32_t msg_size = header[0];
  if (ack_id_p) {
    *ack_id_p = header[1];
  }

  if (msg_size == 0) {
    /* empty message, only an ack_id */
    return 0;
  }

  /* bufp can be NULL only if we expect an empty message (ack_id only) */
  assert(bufp != NULL);

  /* read serialized msg */
  *bufp = (char *) malloc(msg_size);
  if ((ret = fb_read(fd, *bufp, msg_size)) == -1) {
    return ret;
  }
  assert(ret >= (ssize_t) sizeof(uint32_t));
  return ret;
}

/** Send message, delaying all signals in the current thread.
 *  The caller has to take care of thread locking. */
void fb_fbb_send_msg(void *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  fbb_send(fd, ic_msg, 0);

  thread_signal_danger_zone_leave();
}

/** Send message and wait for ACK, delaying all signals in the current thread.
 *  The caller has to take care of thread locking. */
void fb_fbb_send_msg_and_check_ack(void *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  uint32_t ack_num = get_next_ack_id();
  fbb_send(fd, ic_msg, ack_num);

  uint32_t ack_num_resp = 0;
  fb_recv_msg(&ack_num_resp, NULL, fd);
  assert(ack_num_resp == ack_num);

  thread_signal_danger_zone_leave();
}

/** Compare pointers to char* like strcmp() for char* */
static int cmpstringpp(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
     pointers to char", but strcmp(3) arguments are "pointers
     to char", hence the following cast plus dereference */
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}

/** Store file locations for which files open() does not need an ACK. */
static void store_system_locations() {
  char* env_system_locations = getenv("FB_SYSTEM_LOCATIONS");
  string_array_init(&system_locations);
  if (env_system_locations) {
    system_locations_env_str = strdup(env_system_locations);
    char *prefix = system_locations_env_str;
    while (prefix) {
      char *next_prefix = strchr(prefix, ':');
      if (next_prefix) {
        *next_prefix = '\0';
        next_prefix++;
      }
      /* Skip "". */
      if (*prefix != '\0') {
        string_array_append(&system_locations, prefix);
        prefix = next_prefix;
      }
    }
  }
}

/** Add shared library's name to the file list */
static int shared_libs_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  string_array *array = (string_array *) data;
  (void) size;  /* unused */

  if (info->dlpi_name[0] == '\0') {
    /* FIXME does this really happen? */
    return 0;
  }
  const char *libfbintercept = "/libfbintercept.so";
  if (strlen(info->dlpi_name) >= strlen(libfbintercept) &&
    strcmp(info->dlpi_name + strlen(info->dlpi_name)
           - strlen(libfbintercept), libfbintercept) == 0) {
    /* This is internal to Firebuild, filter it out. */
    return 0;
  }
  if (strcmp(info->dlpi_name, "linux-vdso.so.1") == 0) {
    /* This is an in-kernel library, filter it out. */
    return 0;
  }

  string_array_append(array, (/* non-const */ char *) info->dlpi_name);
  return 0;
}

/**
 * Reconnect to the supervisor and reinitialize other stuff in the child
 * after a fork(). Do it from the first registered pthread_atfork
 * handler so that it happens before other such handlers are run.
 * See #237 for further details.
 */
static void atfork_child_handler(void) {
  /* Reinitialize the lock, see #207.
   *
   * We don't know if the lock was previously held, we'd need to check
   * the variable i_am_intercepting from the intercepted fork() which is
   * not available here, and storing it in a thread-global variable is
   * probably not worth the trouble. The intercepted fork() will attempt
   * to unlock if it grabbed the lock, which will silently fail, that's
   * okay. */
  pthread_mutex_init(&ic_global_lock, NULL);

  /* Add a useful trace marker */
  if (insert_trace_markers) {
    char buf[256];
    snprintf(buf, sizeof(buf), "launched via fork() by ppid %d", ic_orig_getppid());
    insert_debug_msg(buf);
  }

  /* Reinitialize other stuff */
  reset_interceptors();
  clear_all_file_states();
  ic_pid = ic_orig_getpid();

  /* Reconnect to supervisor */
  fb_init_supervisor_conn();

  /* Inform the supervisor about who we are */
  FBB_Builder_fork_child ic_msg;
  fbb_fork_child_init(&ic_msg);
  fbb_fork_child_set_pid(&ic_msg, ic_pid);
  fbb_fork_child_set_ppid(&ic_msg, getppid());
  fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
}

static void on_exit_handler(const int status, void *arg) {
  (void) arg;  /* unused */

  insert_debug_msg("our_on_exit_handler-begin");
  handle_exit(status);
  insert_debug_msg("our_on_exit_handler-end");

  /* Destruction of global objects is not done here, because other exit handlers
   * may perform actions that need to be reported to the supervisor.
   * TODO(rbalint) add Valgrind suppress file
   */
}

void handle_exit(const int status) {
  /* On rare occasions (e.g. two threads attempting to exit at the same
   * time) this method is called multiple times. The server can safely
   * handle it. */

  /* Use the same pattern for locking as in tpl.c, simplified (fewer debugging messages). */
  if (intercepting_enabled) {
    bool i_locked = false;
    thread_signal_danger_zone_enter();
    if (!thread_has_global_lock) {
      pthread_mutex_lock(&ic_global_lock);
      thread_has_global_lock = true;
      thread_intercept_on = "handle_exit";
      i_locked = true;
    }
    thread_signal_danger_zone_leave();

    FBB_Builder_exit ic_msg;
    fbb_exit_init(&ic_msg);
    fbb_exit_set_exit_status(&ic_msg, status);

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    fbb_exit_set_utime_u(&ic_msg,
        (int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
    fbb_exit_set_stime_u(&ic_msg,
        (int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);

    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);

    if (i_locked) {
      thread_signal_danger_zone_enter();
      pthread_mutex_unlock(&ic_global_lock);
      thread_has_global_lock = false;
      thread_intercept_on = NULL;
      thread_signal_danger_zone_leave();
    }
  }
}

/**
 * A wrapper in front of the start_routine of a pthread_create(), inserting a useful trace marker.
 * pthread_create()'s two parameters start_routine and arg are accessed via one,
 * malloc()'ed in the intercepted pthread_create() and free()'d here.
 */
void *pthread_start_routine_wrapper(void *routine_and_arg) {
  if (insert_trace_markers) {
    char buf[256];
    snprintf(buf, sizeof(buf), "launched via pthread_create() in pid %d", ic_orig_getpid());
    insert_debug_msg(buf);
  }
  void *(*start_routine)(void *) = ((void **)routine_and_arg)[0];
  void *arg = ((void **)routine_and_arg)[1];
  free(routine_and_arg);
  return (*start_routine)(arg);
}

/**
 * Set up a supervisor connection
 * @param[in] fd if > -1 remap the connection to that fd
 * @return fd of the connection
 */
int fb_connect_supervisor(int fd) {
  int conn_ret = -1, conn = ic_orig_socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

  assert(conn != -1);

  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(remote));
  remote.sun_family = AF_UNIX;
  assert(strlen(fb_conn_string) < sizeof(remote.sun_path));
  strncpy(remote.sun_path, fb_conn_string, sizeof(remote.sun_path));

  conn_ret = ic_orig_connect(conn, (struct sockaddr *)&remote, sizeof(remote));
  if (conn_ret == -1) {
    ic_orig_perror("connect");
    assert(0 && "connection to supervisor failed");
  }

  if (fd > -1 && conn != fd) {
    int ret = ic_orig_dup3(conn, fd, O_CLOEXEC);
    if (ret == -1) {
      ic_orig_perror("dup3");
      assert(0 && "connecting standard fds to supervisor failed");
    }
    ic_orig_close(conn);
    conn = fd;
  }
  return conn;
}

/**  Set up the main supervisor connection */
void fb_init_supervisor_conn() {
  if (fb_conn_string == NULL) {
    fb_conn_string = strdup(getenv("FB_SOCKET"));
  }
  // reconnect to supervisor
  ic_orig_close(fb_sv_conn);
  fb_sv_conn = fb_connect_supervisor(-1);
}

/**
 * Initialize interceptor's data structures and sync with supervisor
 */
static void fb_ic_init() {
  if (getenv("FB_INSERT_TRACE_MARKERS") != NULL) {
    insert_trace_markers = true;
  }

  store_system_locations();

  /* We use an uint64_t as bitmap for delayed signals. Make sure it's okay. */
  assert(SIGRTMAX <= 64);
  /* Can't declare orig_signal_handlers as an array because SIGRTMAX isn't a constant */
  orig_signal_handlers = (void (**)(void)) calloc(SIGRTMAX + 1, sizeof (void (*)(void)));

  init_interceptors();

  assert(thread_intercept_on == NULL);
  thread_intercept_on = "init";
  insert_debug_msg("initialization-begin");

  /* Useful for debugging deadlocks with strace, since the same values appear in futex()
   * if we need to wait for the lock. */
  if (insert_trace_markers) {
    char buf[256];
    snprintf(buf, sizeof(buf), "ic_global_lock = %p", &ic_global_lock);
    insert_debug_msg(buf);
    snprintf(buf, sizeof(buf), "ic_system_popen_lock = %p", &ic_system_popen_lock);
    insert_debug_msg(buf);
  }

  // init global variables

  /* Save a copy of LD_LIBRARY_PATH before someone might modify it. */
  char *llp = getenv("LD_LIBRARY_PATH");
  if (llp != NULL) {
    env_ld_library_path = strdup(llp);
  }

  fb_init_supervisor_conn();

  pthread_atfork(NULL, NULL, atfork_child_handler);
  on_exit(on_exit_handler, NULL);

  char **argv, **env;
  get_argv_env(&argv, &env);

  pid_t pid, ppid;
  ic_pid = pid = ic_orig_getpid();
  ppid = ic_orig_getppid();

  char cwd_buf[CWD_BUFSIZE];
  if (ic_orig_getcwd(cwd_buf, CWD_BUFSIZE) == NULL) {
    assert(0 && "getcwd() returned NULL");
  }

  FBB_Builder_scproc_query ic_msg;
  fbb_scproc_query_init(&ic_msg);

  fbb_scproc_query_set_pid(&ic_msg, pid);
  fbb_scproc_query_set_ppid(&ic_msg, ppid);
  fbb_scproc_query_set_cwd(&ic_msg, cwd_buf);
  fbb_scproc_query_set_arg(&ic_msg, argv);

  /* make a sorted and filtered copy of env */
  int env_len = 0, env_copy_len = 0;
  for (char** cursor = env; *cursor != NULL; cursor++) {
    env_len++;
  }
  char *env_copy[sizeof(env[0]) * (env_len + 1)];

  for (char** cursor = env; *cursor != NULL; cursor++) {
    const char *fb_socket = "FB_SOCKET=";
    const char *fb_system_locations = "FB_SYSTEM_LOCATIONS=";
    if (strncmp(*cursor, fb_socket, strlen(fb_socket)) != 0 &&
        strncmp(*cursor, fb_system_locations, strlen(fb_system_locations)) != 0) {
      env_copy[env_copy_len++] = *cursor;
    }
  }
  env_copy[env_copy_len] = NULL;
  qsort(env_copy, env_copy_len, sizeof(env_copy[0]), cmpstringpp);
  fbb_scproc_query_set_env_var(&ic_msg, env_copy);

  // get full executable path
  // see http://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe
  // and man 2 readlink
  char linkname[CWD_BUFSIZE];
  ssize_t r;
  r = ic_orig_readlink("/proc/self/exe", linkname, CWD_BUFSIZE - 1);
  if (r > 0 && r < CWD_BUFSIZE) {
    linkname[r] = '\0';
    fbb_scproc_query_set_executable(&ic_msg, linkname);
  }

  // list loaded shared libs
  string_array libs;
  string_array_init(&libs);
  dl_iterate_phdr(shared_libs_cb, &libs);
  fbb_scproc_query_set_libs(&ic_msg, libs.p);

  fbb_send(fb_sv_conn, &ic_msg, 0);

  FBB_scproc_resp *sv_msg = NULL;
#ifndef NDEBUG
  ssize_t len =
#endif
      fb_recv_msg(NULL, (char **)&sv_msg, fb_sv_conn);
  assert(len >= (ssize_t) sizeof(int));
  assert(*(int *) sv_msg == FBB_TAG_scproc_resp);
  debug_flags = fbb_scproc_resp_get_debug_flags_with_fallback(sv_msg, 0);

  // we may return immediately if supervisor decides that way
  if (fbb_scproc_resp_get_shortcut(sv_msg)) {
    assert(fbb_scproc_resp_has_exit_status(sv_msg));
    insert_debug_msg("this process was shortcut by the supervisor, exiting");
    void(*orig_underscore_exit)(int) = (void(*)(int)) dlsym(RTLD_NEXT, "_exit");
    (*orig_underscore_exit)(fbb_scproc_resp_get_exit_status(sv_msg));
    assert(0 && "_exit() did not exit");
  }

  free(sv_msg);

  /* pthread_sigmask() is only available if we're linked against libpthread.
   * Otherwise use the single-threaded sigprocmask(). */
  ic_pthread_sigmask = dlsym(RTLD_NEXT, "pthread_sigmask");
  if (!ic_pthread_sigmask) {
    ic_pthread_sigmask = &sigprocmask;
  }

  insert_debug_msg("initialization-end");
  thread_intercept_on = NULL;
  ic_init_done = true;
}

/**
 * Collect information about process the earliest possible, right
 * when interceptor library loads or when the first interceped call happens
 */
void fb_ic_load() {
  if (!ic_init_done) {
    int (*orig_pthread_once)(pthread_once_t *, void (*)(void)) = dlsym(RTLD_NEXT, "pthread_once");
    if (orig_pthread_once) {
      /* Symbol found means that we are linked to libpthread. Use its method to guarantee that we
       * initialize exactly once. */
      (*orig_pthread_once)(&ic_init_control, fb_ic_init);
    } else  {
      /* Symbol not found means that we are not linked to libpthread, i.e. we're single threaded. */
      fb_ic_init();
    }
  }
}

static void fb_ic_cleanup() {
  /* Don't put anything here, unless you really know what you're doing!
   * Our on_exit_handler, which reports the exit code and resource usage
   * to the supervisor, is run _after_ this destructor, and still needs
   * pretty much all the functionality that we have (including the
   * communication channel). */
}


/** wrapper for read() retrying on recoverable errors */
ssize_t fb_read(int fd, void *buf, size_t count) {
  FB_READ_WRITE(*ic_orig_read, fd, buf, count);
}

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(*ic_orig_writev, fd, iov, iovcnt);
}

/** Send error message to supervisor */
extern void fb_error(const char* msg) {
  FBB_Builder_fb_error ic_msg;
  fbb_fb_error_init(&ic_msg);
  fbb_fb_error_set_msg(&ic_msg, msg);
  fb_fbb_send_msg(&ic_msg, fb_sv_conn);
}

/** Send debug message to supervisor if debug level is at least lvl */
void fb_debug(const char* msg) {
  FBB_Builder_fb_debug ic_msg;
  fbb_fb_debug_init(&ic_msg);
  fbb_fb_debug_set_msg(&ic_msg, msg);
  fb_fbb_send_msg(&ic_msg, fb_sv_conn);
}


/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_init():
 * Add an entry, with a new empty string array, to our pool.
 */
void psfa_init(const posix_spawn_file_actions_t *p) {
  psfa_destroy(p);

  /* grow buffer if necessary */
  if (psfas_alloc == 0) {
    psfas_alloc = 4 /* whatever */;
    psfas = (psfa *) malloc(sizeof(psfa) * psfas_alloc);
  } else if (psfas_num == psfas_alloc) {
    psfas_alloc *= 2;
    psfas = (psfa *) realloc(psfas, sizeof(psfa) * psfas_alloc);
  }

  psfas[psfas_num].p = p;
  string_array_init(&psfas[psfas_num].actions);
  psfas_num++;
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_destroy():
 * Remove the entry, freeing up the string array, from our pool.
 * Do not shrink psfas.
 */
void psfa_destroy(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      string_array_deep_free(&psfas[i].actions);
      if (i < psfas_num - 1) {
        /* Keep the array dense by moving the last item to this slot. */
        psfas[i] = psfas[psfas_num - 1];
      }
      psfas_num--;
      /* There can't be more than 1 match. */
      break;
    }
  }
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_addopen():
 * Append a corresponding record to our structures.
 * An open action is denoted using the string "o <fd> <flags> <mode> <filename>"
 * (without the angle brackets).
 */
void psfa_addopen(const posix_spawn_file_actions_t *p,
                  int fd,
                  const char *path,
                  int flags,
                  mode_t mode) {
  string_array *obj = psfa_find(p);
  assert(obj);

  char *str;
  if (asprintf(&str, "o %d %d %d %s", fd, flags, mode, path) < 0) {
    perror("asprintf");
  }
  string_array_append(obj, str);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_addclose():
 * Append a corresponding record to our structures.
 * A close action is denoted using the string "c <fd>" (without the angle brackets).
 */
void psfa_addclose(const posix_spawn_file_actions_t *p,
                   int fd) {
  string_array *obj = psfa_find(p);
  assert(obj);

  char *str;
  if (asprintf(&str, "c %d", fd) < 0) {
    perror("asprintf");
  }
  string_array_append(obj, str);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_adddup2():
 * Append a corresponding record to our structures.
 * A dup2 action is denoted using the string "d <oldfd> <newfd>" (without the angle brackets).
 */
void psfa_adddup2(const posix_spawn_file_actions_t *p,
                  int oldfd,
                  int newfd) {
  string_array *obj = psfa_find(p);
  assert(obj);

  char *str;
  if (asprintf(&str, "d %d %d", oldfd, newfd) < 0) {
    perror("asprintf");
  }
  string_array_append(obj, str);
}

/**
 * Find the string_array for a given posix_spawn_file_actions.
 */
string_array *psfa_find(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      return &psfas[i].actions;
    }
  }
  return NULL;
}
