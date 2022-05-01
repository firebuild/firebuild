/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "interceptor/intercept.h"

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/auxv.h>
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

/** Resource usage at the process' last exec() */
struct rusage initial_rusage;

/** Global lock for preventing parallel system and popen calls */
pthread_mutex_t ic_system_popen_lock = PTHREAD_MUTEX_INITIALIZER;

/** Global lock for serializing critical interceptor actions */
pthread_mutex_t ic_global_lock = PTHREAD_MUTEX_INITIALIZER;

/** Connection string to supervisor */
char fb_conn_string[IC_PATH_BUFSIZE] = {'\0'};

/** Connection string length */
size_t fb_conn_string_len = 0;

/** Connection file descriptor to supervisor */
int fb_sv_conn = -1;

/** pthread_sigmask() if available (libpthread is loaded), otherwise sigprocmask() */
int (*ic_pthread_sigmask)(int, const sigset_t *, sigset_t *);

/** Control for running the initialization exactly once */
pthread_once_t ic_init_control = PTHREAD_ONCE_INIT;

/** Fast check for whether interceptor init has been run */
bool ic_init_done = false;

/** System locations to not ask ACK for when opening them, as set in the environment variable. */
char system_locations_env_buf[4096];

/** System locations to not ask ACK for when opening them. */
STATIC_STRING_ARRAY(system_locations, 32);

bool intercepting_enabled = true;

/** Current working directory as reported to the supervisor */
char ic_cwd[IC_PATH_BUFSIZE] = {0};
size_t ic_cwd_len = 0;

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

void (*orig_signal_handlers[IC_WRAP_SIGRTMAX])(void) = {NULL};

/** Whether to install our wrapper for the given signal. */
bool signal_is_wrappable(int signum) {
  /* Safety check, so that we don't crash if the user passes an invalid value to signal(),
   * sigset() or sigaction(). Just let the original function handle it somehow. */
  if (signum < 1 || signum > IC_WRAP_SIGRTMAX) {
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
    thread_delayed_signals_bitmap |= (1LLU << (signum - 1));
    return;
  }

  thread_signal_handler_running_depth++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-1arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  ((void (*)(int))(*orig_signal_handlers[signum - 1]))(signum);

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
    thread_delayed_signals_bitmap |= (1LLU << (signum - 1));
    // FIXME(egmont) stash "info"
    return;
  }

  thread_signal_handler_running_depth++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  // FIXME(egmont) if this is a re-raised signal from thread_raise_delayed_signals()
  // [can this be detected fully reliably, without the slightest race condition?]
  // then replace "info" with the stashed version
  ((void (*)(int, siginfo_t *, void *))(*orig_signal_handlers[signum - 1]))(signum, info, ucontext);

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-end %d\n", signum);
  insert_debug_msg(debug_msg);

  thread_signal_handler_running_depth--;
}

/** Internal helper for thread_signal_danger_zone_leave(), see there for details. */
void thread_raise_delayed_signals() {
  /* Execute the delayed signals, by re-raising them. */
  char debug_msg[256];
  for (int signum = 1; signum <= IC_WRAP_SIGRTMAX; signum++) {
    if (thread_delayed_signals_bitmap & (1LLU << (signum - 1))) {
      snprintf(debug_msg, sizeof(debug_msg), "raising delayed signal %d\n", signum);
      insert_debug_msg(debug_msg);
      thread_delayed_signals_bitmap &= ~(1LLU << (signum - 1));
      raise(signum);
    }
  }
}

/** Take the global lock if the thread does not hold it already */
void grab_global_lock(bool *i_locked, const char * const function_name) {
  thread_signal_danger_zone_enter();

  /* Some internal integrity assertions */
  if ((thread_has_global_lock) != (thread_intercept_on != NULL)) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf),
             "Internal error while intercepting %s: thread_has_global_lock (%s) and "
             "thread_intercept_on (%s) must go hand in hand",
             function_name, thread_has_global_lock ? "true" : "false", thread_intercept_on);
    insert_debug_msg(debug_buf);
    assert(0 && "Internal error: thread_has_global_lock and "
           "thread_intercept_on must go hand in hand");
  }
  if (thread_signal_handler_running_depth == 0 && thread_libc_nesting_depth == 0
      && thread_intercept_on != NULL) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf),
             "Internal error while intercepting %s: already intercepting %s "
             "(and no signal or atfork handler running in this thread)",
             function_name, thread_intercept_on);
    insert_debug_msg(debug_buf);
    assert(0 && "Internal error: nested interceptors (no signal handler running)");
  }

  if (!thread_has_global_lock) {
    pthread_mutex_lock(&ic_global_lock);
    thread_has_global_lock = true;
    thread_intercept_on = function_name;
    *i_locked = true;
  }
  thread_signal_danger_zone_leave();
  assert(thread_signal_danger_zone_depth == 0);
}

void release_global_lock() {
  thread_signal_danger_zone_enter();
  pthread_mutex_unlock(&ic_global_lock);
  thread_has_global_lock = false;
  thread_intercept_on = NULL;
  thread_signal_danger_zone_leave();
  assert(thread_signal_danger_zone_depth == 0);
}

/** debugging flags */
int32_t debug_flags = 0;

/** Initial LD_LIBRARY_PATH so that we can fix it up if needed */
char env_ld_library_path[IC_PATH_BUFSIZE] = {0};

/** Insert marker open()-s for strace, ltrace, etc. */
bool insert_trace_markers = false;

/** Next ACK id*/
static uint16_t ack_id = 1;

voidp_set popened_streams;

psfa *psfas = NULL;
int psfas_num = 0;
int psfas_alloc = 0;


/** Insert debug message */
void insert_debug_msg(const char* m) {
#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    int saved_errno = errno;
    char tpl[256] = "/FIREBUILD   ###   ";
    IC_ORIG(open)(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1), 0);
    errno = saved_errno;
  }
#else
  (void)m;
#endif
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

/** Get next ACK id */
static uint16_t get_next_ack_id() {
  ack_id++;
  /* Start over after 65535, but skip the value of 0 because that means no ACK is expected. */
  if (ack_id == 0) {
    ack_id = 1;
  }
  return ack_id;
}

/**
 * Receive a message consisting solely of an ack_id.
 *
 * It's the caller's responsibility to lock.
 *
 * @param fd the communication file descriptor
 * @return the received ack_id
 */
static uint16_t fb_recv_ack(int fd) {
  /* read the header */
  msg_header header;
#ifndef NDEBUG
  ssize_t ret =
#endif
      fb_read(fd, &header, sizeof(header));
  assert(ret == sizeof(header));

  assert(header.msg_size == 0);
  assert(header.fd_count == 0);

  return header.ack_id;
}

/** Send the serialized version of the given message over the wire,
 *  prefixed with the ack num and the message length */
void fb_send_msg(int fd, const void /*FBBCOMM_Builder*/ *ic_msg, uint16_t ack_num) {
  int len = fbbcomm_builder_measure(ic_msg);
  char *buf = alloca(sizeof(msg_header) + len);
  memset(buf, 0, sizeof(msg_header));
  fbbcomm_builder_serialize(ic_msg, buf + sizeof(msg_header));
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
  memset(buf, 0, sizeof(msg_header));
  ((msg_header *)buf)->ack_id = ack_num;
  ((msg_header *)buf)->msg_size = len;
#pragma GCC diagnostic pop
  fb_write(fd, buf, sizeof(msg_header) + len);
}

/** Send message, delaying all signals in the current thread.
 *  The caller has to take care of thread locking. */
void fb_fbbcomm_send_msg(const void /*FBBCOMM_Builder*/ *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  fb_send_msg(fd, ic_msg, 0);

  thread_signal_danger_zone_leave();
}

/** Send message and wait for ACK, delaying all signals in the current thread.
 *  The caller has to take care of thread locking. */
void fb_fbbcomm_send_msg_and_check_ack(const void /*FBBCOMM_Builder*/ *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  uint16_t ack_num = get_next_ack_id();
  fb_send_msg(fd, ic_msg, ack_num);

#ifndef NDEBUG
  uint16_t ack_num_resp =
#endif
      fb_recv_ack(fd);
  assert(ack_num_resp == ack_num);

  thread_signal_danger_zone_leave();
}

static bool maybe_send_pre_open_internal(const int dirfd, const char* file, int flags,
                                         bool need_ack) {
  if (file && is_write(flags) && (flags & O_TRUNC)
      && !(flags & (O_EXCL | O_DIRECTORY |O_TMPFILE))) {
    FBBCOMM_Builder_pre_open ic_msg;
    fbbcomm_builder_pre_open_init(&ic_msg);
    fbbcomm_builder_pre_open_set_dirfd(&ic_msg, dirfd);
    BUILDER_MAYBE_SET_ABSOLUTE_CANONICAL(pre_open, dirfd, file);
    if (need_ack) {
      fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    } else {
      fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
    }
    return true;
  } else {
    return false;
  }
}

bool maybe_send_pre_open(const int dirfd, const char* file, int flags) {
  return maybe_send_pre_open_internal(dirfd, file, flags, true);
}

bool maybe_send_pre_open_without_ack_request(const int dirfd, const char* file, int flags) {
  return maybe_send_pre_open_internal(dirfd, file, flags, false);
}

/**
 * Make the filename canonical in place.
 *
 * String operation only, does not look at the actual file system.
 * Removes double slashes, trailing slashes (except if the entire path is "/")
 * and "." components.
 * Preserves ".." components, since they might point elsewhere if a symlink led to
 * its containing directory.
 * See #401 for further details and gotchas.
 *
 * Returns the length of the canonicalized path.
 */
size_t make_canonical(char *path, size_t original_length) {
  char *src = path, *dst = path;  /* dst <= src all the time */
  bool add_slash = true;

  if (path[0] == '\0') return 0;

  if (!(path[0] == '.' && path[1] == '/')) {
    char *a = strstr(path, "//");
    char *b = strstr(path, "/./");
    if (a == NULL && b == NULL) {
      /* This is the quick code path for most of the well-behaved paths:
       * doesn't start with "./", doesn't contain "//" or "/./".
       * If a path passes this check then the only thing that might need
       * fixing is a trailing "/" or "/.". */
      size_t len = original_length;
      if (len >= 2 && path[len - 1] == '.' && path[len - 2] == '/') {
        /* Strip the final "." if the path ends in "/.". */
        len--;
        path[len] = '\0';
      }
      if (len >= 2 && path[len - 1] == '/') {
        /* Strip the final "/" if the path ends in "/" and that's not the entire path. */
        len--;
        path[len] = '\0';
      }
      /* The quick code path is done here. */
      return len;
    }
    /* Does not start with "./", but contains at least a "//" or "/./".
     * Everything is fine up to that point. Fast forward src and dst. */
    if (a != NULL && b != NULL) {
      src = dst = a < b ? a : b;
    } else if (a != NULL) {
      src = dst = a;
    } else {
      src = dst = b;
    }
  } else {
    /* Starts with "./", needs fixing from the beginning. */
    src++;
    add_slash = false;  /* Don't add "/" to dst when skipping the first one(s) in src. */
  }

  while (src[0] != '\0') {
    /* Skip through a possible run of slashes and non-initial "." components, e.g. "//././". */
    if (src[0] == '/') {
      while (src[0] == '/' || (src[0] == '.' && (src[1] == '/' || src[1] == '\0'))) src++;
      if (add_slash) {
        *dst++ = '/';
      }
    }
    /* Handle a regular (not ".") component. */
    while (src[0] != '/' && src[0] != '\0') {
      *dst++ = *src++;
    }
    add_slash = true;
  }

  /* If got empty path then it should be a "." instead. */
  if (dst == path) {
    *dst++ = '.';
  }
  /* Strip trailing slash, except if the entire path is "/". */
  if (dst > path + 1 && dst[-1] == '/') {
    dst--;
  }

  *dst = '\0';
  return dst - path;
}

#if 0  /* unittests for make_canonical() */

/* Macro so that assert() reports useful line numbers. */
#define test(A, B) { \
  char *str = strdup(A); \
  make_canonical(str, strlen(str)); \
  if (strcmp(str, B)) { \
    fprintf(stderr, "Error:  input: %s\n", A); \
    fprintf(stderr, "     expected: %s\n", B); \
    fprintf(stderr, "  got instead: %s\n", str); \
  } \
}

int main() {
  test("/", "/");
  test("/etc/hosts", "/etc/hosts");
  test("/usr/include/vte-2.91/vte/vteterminal.h", "/usr/include/vte-2.91/vte/vteterminal.h");
  test("/usr/bin/", "/usr/bin");
  test("/usr/bin/.", "/usr/bin");
  test("/usr/./bin", "/usr/bin");
  test("/./usr/bin", "/usr/bin");
  test("//", "/");
  test("", "");
  test(".", ".");
  test("/.", "/");
  test("./", ".");
  test("/./././", "/");
  test("./././.", ".");
  test("//foo//bar//", "/foo/bar");
  test("/././foo/././bar/././", "/foo/bar");
  test("///.//././/.///foo//.//bar//.", "/foo/bar");
  test("////foo/../bar", "/foo/../bar");
  test("/foo/bar/../../../../../", "/foo/bar/../../../../..");
  test("/.foo/.bar/..quux", "/.foo/.bar/..quux");
  test("foo", "foo");
  test("foo/bar", "foo/bar");
  test("././foo/./bar/./.", "foo/bar");
}

#endif  /* unittests for canonicalize_path() */

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
  if (env_system_locations) {
    strncpy(system_locations_env_buf, env_system_locations, sizeof(system_locations_env_buf));
    const size_t env_system_locations_len = strlen(env_system_locations);
    if (env_system_locations_len + 1 > sizeof(system_locations_env_buf)) {
      /* Trim to the fitting parts. The locations are used only for improving
       * performance and the space is allocated statically. */
      system_locations_env_buf[sizeof(system_locations_env_buf) - 1] = '\0';
      char * last_separator = strrchr(system_locations_env_buf, ':');
      if (!last_separator) {
        /* This is a quite long single path that may be incomplete, thus ignore it. */
        system_locations_env_buf[0] = '\0';
      } else {
        /* Drop the possibly incomplete path after the last separator.*/
        *last_separator = '\0';
      }
    }
    char *prefix = system_locations_env_buf;
    /* Process all locations that fit system_location without reallocation. */
    while (prefix && !is_string_array_full(&system_locations)) {
      char *next_prefix = strchr(prefix, ':');
      if (next_prefix) {
        *next_prefix = '\0';
        next_prefix++;
      }
      /* Skip "". */
      if (*prefix != '\0') {
        string_array_append_noalloc(&system_locations, prefix);
        prefix = next_prefix;
      }
    }
  }
}

static bool skip_shared_lib(const char *name, const size_t len) {
  if (name[0] == '\0') {
    /* FIXME does this really happen? */
    return true;
  }
  const char *libfirebuild = "/" LIBFIREBUILD_SO;
  if (len >= strlen(libfirebuild) &&
      strcmp(name + strlen(name)
             - strlen(libfirebuild), libfirebuild) == 0) {
    /* This is internal to Firebuild, filter it out. */
    return true;
  }
  if (strcmp(name, "linux-vdso.so.1") == 0) {
    /* This is an in-kernel library, filter it out. */
    return true;
  }
  return false;
}

/**
 * State struct for shared_libs_cb()
 */
typedef struct shared_libs_cb_data_ {
  /** Array of collected shared library names. */
  string_array *array;
  /** Number of entries that could be collected to `array`. */
  int collectable_entries;
  /** Number of entries that are not in canonical form, thus need to be made canonical. */
  int not_canonical_entries;
  /** Buffert to store canonized library names. Size is canonized_libs_size * IC_PATH_BUFSIZE. */
  char *canonized_libs;
  /** Number of canonized names canonized_libs can store. */
  int canonized_libs_size;
  /** Number of canonized names stored in canonized_libs. */
  int canonized_libs_count;
} shared_libs_cb_data_t;

/** Add shared library's name to the file list */
static int shared_libs_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  (void) size;  /* unused */
  shared_libs_cb_data_t *cb_data = (shared_libs_cb_data_t *)data;
  string_array *array = cb_data->array;

  const char* name = info->dlpi_name;
  const size_t len = strlen(name);
  if (skip_shared_lib(name, len)) {
    return 0;
  }
  cb_data->collectable_entries++;
  if (is_canonical(name, len)) {
    if (!is_string_array_full(array)) {
      string_array_append_noalloc(array, (/* non-const */ char *) name);
    }
  } else {
    /* !is_canonical() */
    cb_data->not_canonical_entries++;
    assert(cb_data->canonized_libs_count <= cb_data->canonized_libs_size);
    if (cb_data->canonized_libs_count < cb_data->canonized_libs_size) {
      /* The there is enough space for the new canonized entry. */
      char * canonical_name =
          &cb_data->canonized_libs[cb_data->canonized_libs_count++ * IC_PATH_BUFSIZE];
      memcpy(canonical_name, name, len + 1);
      make_canonical(canonical_name, len);
      if (!is_string_array_full(array)) {
        string_array_append_noalloc(array, canonical_name);
      }
    }
  }
  return 0;
}

/**
 * Notify the supervisor after a fork(). Do it from the first registered pthread_atfork
 * handler so that it happens before other such handlers are run.
 * See #819 for further details.
 */
static void atfork_parent_handler(void) {
  /* The variable i_am_intercepting from the intercepted fork() is
   * not available here, and storing it in a thread-global variable is
   * probably not worth the trouble. */
  if (intercepting_enabled) {
    FBBCOMM_Builder_fork_parent ic_msg;
    fbbcomm_builder_fork_parent_init(&ic_msg);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
}

/**
 * Reconnect to the supervisor and reinitialize other stuff in the child
 * after a fork(). Do it from the first registered pthread_atfork
 * handler so that it happens before other such handlers are run.
 * See #237 for further details.
 */
static void atfork_child_handler(void) {
  /* ic_pid still have parent process' pid */
  pid_t ppid = ic_pid;
  /* Reset, getrusage will report the correct self resource usage. */
  timerclear(&initial_rusage.ru_stime);
  timerclear(&initial_rusage.ru_utime);
  /* Reinitialize the lock, see #207.
   *
   * We don't know if the lock was previously held, we'd need to check
   * the variable i_am_intercepting from the intercepted fork() which is
   * not available here, and storing it in a thread-global variable is
   * probably not worth the trouble. The intercepted fork() will attempt
   * to unlock if it grabbed the lock, which will silently fail, that's
   * okay. */
  if (intercepting_enabled) {
    pthread_mutex_init(&ic_global_lock, NULL);

    /* Add a useful trace marker */
    if (insert_trace_markers) {
      char buf[256];
      snprintf(buf, sizeof(buf), "launched via fork() by ppid %d", ppid);
      insert_debug_msg(buf);
    }

    /* Reinitialize other stuff */
    reset_interceptors();
    ic_pid = IC_ORIG(getpid)();

    /* Reconnect to supervisor */
    fb_init_supervisor_conn();

    /* Inform the supervisor about who we are */
    FBBCOMM_Builder_fork_child ic_msg;
    fbbcomm_builder_fork_child_init(&ic_msg);
    fbbcomm_builder_fork_child_set_pid(&ic_msg, ic_pid);
    fbbcomm_builder_fork_child_set_ppid(&ic_msg, ppid);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
}

static void atexit_handler() {
  insert_debug_msg("our_atexit_handler-begin");
  handle_exit();
  insert_debug_msg("our_atexit_handler-end");

  /* Destruction of global objects is not done here, because other exit handlers
   * may perform actions that need to be reported to the supervisor.
   * TODO(rbalint) add Valgrind suppress file
   */
}

void handle_exit() {
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

    FBBCOMM_Builder_rusage ic_msg;
    fbbcomm_builder_rusage_init(&ic_msg);

    struct rusage ru;
    IC_ORIG(getrusage)(RUSAGE_SELF, &ru);
    timersub(&ru.ru_stime, &initial_rusage.ru_stime, &ru.ru_stime);
    timersub(&ru.ru_utime, &initial_rusage.ru_utime, &ru.ru_utime);
    fbbcomm_builder_rusage_set_utime_u(&ic_msg,
        (int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
    fbbcomm_builder_rusage_set_stime_u(&ic_msg,
        (int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);

    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);

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
    snprintf(buf, sizeof(buf), "launched via pthread_create() in pid %d", IC_ORIG(getpid)());
    insert_debug_msg(buf);
  }
  void *(*start_routine)(void *) = ((void **)routine_and_arg)[0];
  void *arg = ((void **)routine_and_arg)[1];
  free(routine_and_arg);
  return (*start_routine)(arg);
}

/**
 * Set up a supervisor connection
 * @return fd of the connection
 */
int fb_connect_supervisor() {
  int conn = TEMP_FAILURE_RETRY(IC_ORIG(socket)(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  assert(conn != -1);

  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(remote));
  remote.sun_family = AF_UNIX;
#ifdef FB_EXTRA_DEBUG
  assert(strlen(fb_conn_string) < sizeof(remote.sun_path));
#endif
  strncpy(remote.sun_path, fb_conn_string, sizeof(remote.sun_path));

  int conn_ret = TEMP_FAILURE_RETRY(
      IC_ORIG(connect)(conn, (struct sockaddr *)&remote, sizeof(remote)));
  if (conn_ret == -1) {
    IC_ORIG(perror)("connect");
    assert(0 && "connection to supervisor failed");
  }
  return conn;
}

/**  Set up the main supervisor connection */
void fb_init_supervisor_conn() {
  if (fb_conn_string[0] == '\0') {
    strncpy(fb_conn_string, getenv("FB_SOCKET"), sizeof(fb_conn_string));
    fb_conn_string_len = strlen(fb_conn_string);
  }
  /* Reconnect to supervisor.
   * POSIX says to retry close() on EINTR (e.g. wrap in TEMP_FAILURE_RETRY())
   * but Linux probably disagrees, see #723. */
  IC_ORIG(close)(fb_sv_conn);
  fb_sv_conn = fb_connect_supervisor();
}

/**
 * Initialize interceptor's data structures and sync with supervisor
 */
static void fb_ic_init() {
  IC_ORIG(getrusage)(RUSAGE_SELF, &initial_rusage);

  if (getenv("FB_INSERT_TRACE_MARKERS") != NULL) {
    insert_trace_markers = true;
  }

  store_system_locations();

  /* We use an uint64_t as bitmap for delayed signals. Make sure it's okay. */
  assert(SIGRTMAX <= IC_WRAP_SIGRTMAX);

  voidp_set_init(&popened_streams);

  init_interceptors();

  assert(thread_intercept_on == NULL);
  thread_intercept_on = "init";
  insert_debug_msg("initialization-begin");

  set_all_notify_on_read_write_states();

  /* Useful for debugging deadlocks with strace, since the same values appear in futex()
   * if we need to wait for the lock. */
  if (insert_trace_markers) {
    char buf[256];
    snprintf(buf, sizeof(buf), "ic_global_lock = %p", &ic_global_lock);
    insert_debug_msg(buf);
    snprintf(buf, sizeof(buf), "ic_system_popen_lock = %p", &ic_system_popen_lock);
    insert_debug_msg(buf);
  }

  /* init global variables */

  /* Save a copy of LD_LIBRARY_PATH before someone might modify it. */
  char *llp = getenv("LD_LIBRARY_PATH");
  if (llp != NULL) {
    strncpy(env_ld_library_path, llp, sizeof(env_ld_library_path) - 1);
  }

  fb_init_supervisor_conn();

  pthread_atfork(NULL, atfork_parent_handler, atfork_child_handler);
  atexit(atexit_handler);

  char **argv, **env;
  get_argv_env(&argv, &env);

  pid_t pid, ppid;
  ic_pid = pid = IC_ORIG(getpid)();
  ppid = IC_ORIG(getppid)();

  if (IC_ORIG(getcwd)(ic_cwd, sizeof(ic_cwd)) == NULL) {
    assert(0 && "getcwd() returned NULL");
  }
  ic_cwd_len = strlen(ic_cwd);

  FBBCOMM_Builder_scproc_query ic_msg;
  fbbcomm_builder_scproc_query_init(&ic_msg);

  fbbcomm_builder_scproc_query_set_version(&ic_msg, FIREBUILD_VERSION);

  fbbcomm_builder_scproc_query_set_pid(&ic_msg, pid);
  fbbcomm_builder_scproc_query_set_ppid(&ic_msg, ppid);
  fbbcomm_builder_scproc_query_set_cwd(&ic_msg, ic_cwd);
  fbbcomm_builder_scproc_query_set_arg(&ic_msg, (const char **) argv);

  const char *executed_path = (const char*)getauxval(AT_EXECFN);
  if (executed_path) {
    BUILDER_SET_CANONICAL(scproc_query, executed_path);
  }

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
  fbbcomm_builder_scproc_query_set_env_var(&ic_msg, (const char **) env_copy);

  /* get full executable path
   * see http://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe
   * and readlink(2) */
  char linkname[IC_PATH_BUFSIZE];
  ssize_t r;
  r = IC_ORIG(readlink)("/proc/self/exe", linkname, IC_PATH_BUFSIZE - 1);
  if (r > 0 && r < IC_PATH_BUFSIZE) {
    linkname[r] = '\0';
    fbbcomm_builder_scproc_query_set_executable_with_length(&ic_msg, linkname, r);
  }

  /* list loaded shared libs */
  STATIC_STRING_ARRAY(libs, 64);
  int canonized_libs_size = 8;
  char *canonized_libs = alloca(canonized_libs_size * IC_PATH_BUFSIZE);
  shared_libs_cb_data_t cb_data = {&libs, 0, 0, canonized_libs, canonized_libs_size, 0};
  dl_iterate_phdr(shared_libs_cb, &cb_data);
  if (cb_data.collectable_entries > cb_data.array->len) {
    if (cb_data.not_canonical_entries > canonized_libs_size) {
      /* canonized_libs was not big enough. */
      canonized_libs_size = cb_data.not_canonical_entries;
      canonized_libs = alloca(canonized_libs_size * IC_PATH_BUFSIZE);
    }
    /* The initially allocated space was not enough to collect all shared libs, trying again. */
    if (cb_data.collectable_entries > cb_data.array->size_alloc - 1) {
      /* libs array was not big enough. */
      libs.p = alloca((cb_data.collectable_entries + 1) * sizeof(char*));
      libs.size_alloc = cb_data.collectable_entries + 1;
    } else {
      /* The size was big enough, reset the contents*/
      memset(libs.p, 0, libs.len * sizeof(char*));
    }
    libs.len = 0;

    shared_libs_cb_data_t cb_data2 = {&libs, 0, 0, canonized_libs, canonized_libs_size, 0};
    dl_iterate_phdr(shared_libs_cb, &cb_data2);
    assert(cb_data.collectable_entries == cb_data2.array->len);
  }
  fbbcomm_builder_scproc_query_set_libs(&ic_msg, (const char **) libs.p);

  fb_send_msg(fb_sv_conn, &ic_msg, 0);

  /* Read the scproc_resp message header. */
  msg_header header;
  ssize_t ret = fb_read(fb_sv_conn, &header, sizeof(header));
  assert(ret == sizeof(header));
  assert(header.msg_size > 0);
  uint16_t fd_count = header.fd_count;

  /* Read the scproc_resp message body.
   *
   * This message may have file descriptors attached as ancillary data. */
  FBBCOMM_Serialized *sv_msg_generic = alloca(header.msg_size);

  void *anc_buf = NULL;
  size_t anc_buf_size = 0;
  if (fd_count > 0) {
    anc_buf_size = CMSG_SPACE(fd_count * sizeof(int));
    anc_buf = alloca(anc_buf_size);
    memset(anc_buf, 0, anc_buf_size);
  }

  struct iovec iov = { 0 };
  iov.iov_base = sv_msg_generic;
  iov.iov_len = header.msg_size;

  struct msghdr msgh = { 0 };
  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  msgh.msg_control = anc_buf;
  msgh.msg_controllen = anc_buf_size;

  /* This is the first message arriving on the socket, and it's reasonably small.
   * We can safely expect that the header and the payload are fully available (no short read).
   * However, a signal interrupt might occur. */
  do {
    ret = recvmsg(fb_sv_conn, &msgh, 0);
  } while (ret < 0 && errno == EINTR);
  assert(ret == header.msg_size);
  assert(fbbcomm_serialized_get_tag(sv_msg_generic) == FBBCOMM_TAG_scproc_resp);

  FBBCOMM_Serialized_scproc_resp *sv_msg = (FBBCOMM_Serialized_scproc_resp *) sv_msg_generic;
  debug_flags = fbbcomm_serialized_scproc_resp_get_debug_flags_with_fallback(sv_msg, 0);

  /* we may return immediately if supervisor decides that way */
  if (fbbcomm_serialized_scproc_resp_get_shortcut(sv_msg)) {
    assert(fbbcomm_serialized_scproc_resp_has_exit_status(sv_msg));
    insert_debug_msg("this process was shortcut by the supervisor, exiting");
    void(*orig_underscore_exit)(int) = (void(*)(int)) dlsym(RTLD_NEXT, "_exit");
    (*orig_underscore_exit)(fbbcomm_serialized_scproc_resp_get_exit_status(sv_msg));
    assert(0 && "_exit() did not exit");
  }

  if (fbbcomm_serialized_scproc_resp_has_dont_intercept(sv_msg)) {
    /* if set, must be true */
    assert(fbbcomm_serialized_scproc_resp_get_dont_intercept(sv_msg));
    intercepting_enabled = false;
    env_purge(environ);
  }

  /* Reopen the fds.
   *
   * The current temporary fd numbers were received as ancillary data, and are in the corresponding
   * slot of CMSG_DATA(cmsg).
   *
   * The list of desired final file descriptors are in the received FBB message. There might be
   * multiple desired slots (dups of each other) for each received fd.
   *
   * We're always reopening to a slot that should currently be open in the interceptor (and thus
   * we'll implicitly close that by the dup2). So the set of source fds and the set of targets fds
   * are disjoint. We don't have to worry about dup2ing to a target fd which we'd later need to use
   * as a source fd.
   */
  assert(fd_count == fbbcomm_serialized_scproc_resp_get_reopen_fds_count(sv_msg));
  if (fd_count > 0) {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
    assert(cmsg);
    assert(cmsg->cmsg_level == SOL_SOCKET);
    assert(cmsg->cmsg_type == SCM_RIGHTS);
    assert(cmsg->cmsg_len == CMSG_LEN(fd_count * sizeof(int)));

#ifdef FB_EXTRA_DEBUG
    /* Assert that the set of source fds and the set of target fds are disjoint. */
    for (size_t i = 0; i < fbbcomm_serialized_scproc_resp_get_reopen_fds_count(sv_msg); i++) {
      FBBCOMM_Serialized_scproc_resp_reopen_fd *fds = (FBBCOMM_Serialized_scproc_resp_reopen_fd *)
          fbbcomm_serialized_scproc_resp_get_reopen_fds_at(sv_msg, i);
      assert(fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_count(fds) >= 1);
      for (size_t j = 0; j < fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_count(fds); j++) {
        int dst_fd = fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_at(fds, j);
        for (size_t k = 0; k < fd_count; k++) {
          int src_fd;
          memcpy(&src_fd, CMSG_DATA(cmsg) + k * sizeof(int), sizeof(int));
          assert(src_fd != dst_fd);
        }
      }
    }
#endif

    /* For each source fd, dup2 it to all the desired target fds and then close the source fd. */
    for (size_t i = 0; i < fbbcomm_serialized_scproc_resp_get_reopen_fds_count(sv_msg); i++) {
      FBBCOMM_Serialized_scproc_resp_reopen_fd *fds = (FBBCOMM_Serialized_scproc_resp_reopen_fd *)
          fbbcomm_serialized_scproc_resp_get_reopen_fds_at(sv_msg, i);
      int src_fd;
      memcpy(&src_fd, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));

      /* Preserve the fcntl(..., F_SETFL, ...) mode.
       * The supervisor doesn't track this value, so take the old local fd as reference. If there
       * are more than one local fds, they are supposedly dups of each other (at least this is what
       * the supervisor's bookkeeping said) and thus share these flags, so arbitrarily use the first
       * one as reference.
       * Similarly, since the targets will be dups of each other, it's enough to set the flags once.
       * In fact, set them on the source fd just because it's simpler this way. */
      int flags =
          IC_ORIG(fcntl)(fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_at(fds, 0), F_GETFL);
      assert(flags != -1);
#ifndef NDEBUG
      int fcntl_ret =
#endif
          IC_ORIG(fcntl)(src_fd, F_SETFL, flags);
      assert(fcntl_ret != -1);

      /* Dup2 the source fd to the desired places and then close the original. */
      for (size_t j = 0; j < fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_count(fds); j++) {
        int dst_fd = fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_at(fds, j);
#ifndef NDEBUG
        int dup2_ret =
#endif
            IC_ORIG(dup2)(src_fd, dst_fd);
        assert(dup2_ret == dst_fd);
      }
      IC_ORIG(close)(src_fd);
    }
  }

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
   * Our atexit_handler, which reports the resource usage to the supervisor,
   * is run _after_ this destructor, and still needs pretty much all the
   * functionality that we have (including the communication channel). */
}


/** wrapper for read() retrying on recoverable errors (EINTR and short read) */
ssize_t fb_read(int fd, void *buf, size_t count) {
  FB_READ_WRITE(*IC_ORIG(read), fd, buf, count);
}

/** wrapper for write() retrying on recoverable errors (EINTR and short write) */
ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(*IC_ORIG(write), fd, buf, count);
}

/** Send error message to supervisor */
extern void fb_error(const char* msg) {
  FBBCOMM_Builder_fb_error ic_msg;
  fbbcomm_builder_fb_error_init(&ic_msg);
  fbbcomm_builder_fb_error_set_msg(&ic_msg, msg);
  fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
}

/** Send debug message to supervisor if debug level is at least lvl */
void fb_debug(const char* msg) {
  FBBCOMM_Builder_fb_debug ic_msg;
  fbbcomm_builder_fb_debug_init(&ic_msg);
  fbbcomm_builder_fb_debug_set_msg(&ic_msg, msg);
  fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
}


/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_init():
 * Add an entry, with a new empty string array, to our pool.
 */
void psfa_init(const posix_spawn_file_actions_t *p) {
  // FIXME guard with mutex!

  /* This provides extra safety, in case a previous record belonging to this pointer wasn't cleaned
   * up, and now the same pointer is getting reused for a brand new posix_spawn_file_actions. */
  psfa_destroy(p);

  /* grow buffer if necessary */
  if (psfas_alloc == 0) {
    psfas_alloc = 4  /* whatever */;
    psfas = (psfa *) malloc(sizeof(psfa) * psfas_alloc);
  } else if (psfas_num == psfas_alloc) {
    psfas_alloc *= 2;
    psfas = (psfa *) realloc(psfas, sizeof(psfa) * psfas_alloc);
  }

  psfas[psfas_num].p = p;
  voidp_array_init(&psfas[psfas_num].actions);
  psfas_num++;
}

static void psfa_item_free(void *p) {
  if (fbbcomm_builder_get_tag(p) == FBBCOMM_TAG_posix_spawn_file_action_open) {
    /* For addopen() actions the filename needs to be freed. */
    FBBCOMM_Builder_posix_spawn_file_action_open *builder = p;
    char *path =
        (/* non-const */ char *) fbbcomm_builder_posix_spawn_file_action_open_get_path(builder);
    free(path);
  }
  free(p);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_destroy():
 * Remove the entry, freeing up the string array, from our pool.
 * Do not shrink psfas.
 */
void psfa_destroy(const posix_spawn_file_actions_t *p) {
  // FIXME guard with mutex!

  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      voidp_array_deep_free(&psfas[i].actions, psfa_item_free);
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
 * Append a corresponding FBBCOMM_Builder_posix_spawn_file_action_open builder to our structures.
 */
void psfa_addopen(const posix_spawn_file_actions_t *p,
                  int fd,
                  const char *path,
                  int flags,
                  mode_t mode) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_open *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_open));
  fbbcomm_builder_posix_spawn_file_action_open_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_open_set_fd(fbbcomm_builder, fd);
  fbbcomm_builder_posix_spawn_file_action_open_set_path(fbbcomm_builder, strdup(path));
  fbbcomm_builder_posix_spawn_file_action_open_set_flags(fbbcomm_builder, flags);
  fbbcomm_builder_posix_spawn_file_action_open_set_mode(fbbcomm_builder, mode);

  voidp_array_append(obj, fbbcomm_builder);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_addclose():
 * Append a corresponding FBBCOMM_Builder_posix_spawn_file_action_close builder to our structures.
 */
void psfa_addclose(const posix_spawn_file_actions_t *p,
                   int fd) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_close *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_close));
  fbbcomm_builder_posix_spawn_file_action_close_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_close_set_fd(fbbcomm_builder, fd);

  voidp_array_append(obj, fbbcomm_builder);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_adddup2():
 * Append a corresponding FBBCOMM_Builder_posix_spawn_file_action_dup2 builder to our structures.
 */
void psfa_adddup2(const posix_spawn_file_actions_t *p,
                  int oldfd,
                  int newfd) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_dup2 *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_dup2));
  fbbcomm_builder_posix_spawn_file_action_dup2_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_dup2_set_oldfd(fbbcomm_builder, oldfd);
  fbbcomm_builder_posix_spawn_file_action_dup2_set_newfd(fbbcomm_builder, newfd);

  voidp_array_append(obj, fbbcomm_builder);
}

/**
 * Find the voidp_array for a given posix_spawn_file_actions.
 */
voidp_array *psfa_find(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      return &psfas[i].actions;
    }
  }
  return NULL;
}
