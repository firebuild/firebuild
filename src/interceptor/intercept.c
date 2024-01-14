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


#include "interceptor/intercept.h"

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __APPLE__
#include "libproc.h"
#endif
#ifdef __linux__
#include <link.h>
#endif
#include <pthread.h>
#ifdef __linux__
#include <sys/auxv.h>
#endif
#include <sys/un.h>
#include <sys/resource.h>
#include <spawn.h>

#include "interceptor/env.h"
#include "interceptor/ic_file_ops.h"
#include "interceptor/interceptors.h"
#include "common/firebuild_common.h"

#if defined(__s390x__) || defined (__powerpc64__)
#define VDSO_NAME "linux-vdso64.so.1"
#elif defined(__i386__)
#define VDSO_NAME "linux-gate.so.1"
#else
#define VDSO_NAME "linux-vdso.so.1"
#endif

static void fb_ic_init_constructor(int argc, char **argv) __attribute__((constructor));
static void fb_ic_cleanup() __attribute__((destructor));

fd_state ic_fd_states[IC_FD_STATES_SIZE];

struct rusage initial_rusage;

pthread_mutex_t ic_system_popen_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t ic_global_lock = PTHREAD_MUTEX_INITIALIZER;

char fb_conn_string[FB_PATH_BUFSIZE] = {'\0'};
size_t fb_conn_string_len = 0;

int fb_sv_conn = -1;

bool ic_called_syscall[IC_CALLED_SYSCALL_SIZE] = {0};

bool ic_init_started = false;
bool ic_init_done = false;

/** System locations to not ask ACK for when opening them, as set in the environment variable. */
char read_only_locations_env_buf[4096];

/** Ignore locations to not ask ACK for when opening them, as set in the environment variable. */
char ignore_locations_env_buf[4096];

/**
 * Jobserver users for which the jobserver fds have to be detected, as set in the environment
 * variable. */
char jobserver_users_env_buf[4096];

STATIC_CSTRING_VIEW_ARRAY(read_only_locations, 32);
STATIC_CSTRING_VIEW_ARRAY(ignore_locations, 32);
STATIC_CSTRING_VIEW_ARRAY(jobserver_users, 8);

bool intercepting_enabled = true;

char ic_cwd[FB_PATH_BUFSIZE] = {0};
size_t ic_cwd_len = 0;

/** Program's argc and argv. */
static int ic_argc;
static char **ic_argv;

int ic_pid;

__thread thread_data fb_thread_data = {NULL, 0, 0, 0, false};
#if !defined(FB_ALWAYS_USE_THREAD_LOCAL)
thread_data fb_global_thread_data = {NULL, 0, 0, 0, false};
bool thread_locals_usable = false;
/* Optimization is disabled because when the function is optimized it tries to resolve
 * the address of fb_thread_data, which causes _tlv_bootstrap aborting until
 * the dyld binds the resolver function.
 * See: https://stackoverflow.com/questions/72955200/how-does-macos-dyld-prepare-the-thread-local */
thread_data* get_thread_data() __attribute__((optnone)) {
  return thread_locals_usable ? &fb_thread_data : &fb_global_thread_data;
}
#endif

#ifdef __APPLE__
/* OS X does not support RT signals https://flylib.com/books/en/3.126.1.110/1/ ,
 * but we can handle 64 signals safely. */
#define SIGRTMAX ((int)sizeof(fb_global_thread_data.delayed_signals_bitmap) * 8)
#endif

void (*orig_signal_handlers[IC_WRAP_SIGRTMAX])(void) = {NULL};

bool signal_is_wrappable(int signum) {
  /* Safety check, so that we don't crash if the user passes an invalid value to signal(),
   * sigset() or sigaction(). Just let the original function handle it somehow. */
  if (signum < 1 || signum > IC_WRAP_SIGRTMAX) {
    return false;
  }

  return true;
}

void wrapper_signal_handler_1arg(int signum) {
  char debug_msg[256];

  if (FB_THREAD_LOCAL(signal_danger_zone_depth) > 0) {
    snprintf(debug_msg, sizeof(debug_msg), "signal %d arrived in danger zone, delaying\n", signum);
    insert_debug_msg(debug_msg);
    FB_THREAD_LOCAL(delayed_signals_bitmap) |= (1LLU << (signum - 1));
    return;
  }

  FB_THREAD_LOCAL(interception_recursion_depth)++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-1arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  ((void (*)(int))(*orig_signal_handlers[signum - 1]))(signum);

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-1arg-end %d\n", signum);
  insert_debug_msg(debug_msg);

  FB_THREAD_LOCAL(interception_recursion_depth)--;
}

void wrapper_signal_handler_3arg(int signum, siginfo_t *info, void *ucontext) {
  char debug_msg[256];

  if (FB_THREAD_LOCAL(signal_danger_zone_depth) > 0) {
    snprintf(debug_msg, sizeof(debug_msg), "signal %d arrived in danger zone, delaying\n", signum);
    insert_debug_msg(debug_msg);
    FB_THREAD_LOCAL(delayed_signals_bitmap) |= (1LLU << (signum - 1));
    // FIXME(egmont) stash "info"
    return;
  }

  FB_THREAD_LOCAL(interception_recursion_depth)++;

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-begin %d\n", signum);
  insert_debug_msg(debug_msg);

  // FIXME(egmont) if this is a re-raised signal from thread_raise_delayed_signals()
  // [can this be detected fully reliably, without the slightest race condition?]
  // then replace "info" with the stashed version
  ((void (*)(int, siginfo_t *, void *))(*orig_signal_handlers[signum - 1]))(signum, info, ucontext);

  snprintf(debug_msg, sizeof(debug_msg), "signal-handler-3arg-end %d\n", signum);
  insert_debug_msg(debug_msg);

  FB_THREAD_LOCAL(interception_recursion_depth)--;
}

void thread_raise_delayed_signals() {
  /* Execute the delayed signals, by re-raising them. */
  char debug_msg[256];
  for (int signum = 1; signum <= IC_WRAP_SIGRTMAX; signum++) {
    if (FB_THREAD_LOCAL(delayed_signals_bitmap) & (1LLU << (signum - 1))) {
      snprintf(debug_msg, sizeof(debug_msg), "raising delayed signal %d\n", signum);
      insert_debug_msg(debug_msg);
      FB_THREAD_LOCAL(delayed_signals_bitmap) &= ~(1LLU << (signum - 1));
      raise(signum);
    }
  }
}

int ic_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
  /* pthread_sigmask() is only available if we're linked against libpthread.
   * Otherwise use the single-threaded sigprocmask(). */
#if defined(__APPLE__) || FB_GLIBC_PREREQ(2, 34)
  return pthread_sigmask(how, set, oldset);
#else
  static int (*ic_orig_pthread_sigmask)(int, const sigset_t *, sigset_t *);
  static bool tried_dlsym = false;

  if (ic_orig_pthread_sigmask) {
    return ic_orig_pthread_sigmask(how, set, oldset);
  } else {
    if (!tried_dlsym) {
      ic_orig_pthread_sigmask = dlsym(RTLD_NEXT, "pthread_sigmask");
      tried_dlsym = true;
      /* Try again with possibly resolved symbol. */
      return ic_pthread_sigmask(how, set, oldset);
    } else {
      return sigprocmask(how, set, oldset);
    }
  }
#endif
}

void grab_global_lock(bool *i_locked, const char * const function_name) {
  thread_signal_danger_zone_enter();

  /* Some internal integrity assertions */
  if (FB_THREAD_LOCAL(has_global_lock) != (FB_THREAD_LOCAL(intercept_on) != NULL)) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf),
             "Internal error while intercepting %s: has_global_lock (%s) and "
             "intercept_on (%s) must go hand in hand",
             function_name, FB_THREAD_LOCAL(has_global_lock) ? "true" : "false",
             FB_THREAD_LOCAL(intercept_on));
    insert_debug_msg(debug_buf);
    assert(0 && "Internal error: has_global_lock and intercept_on must go hand in hand");
  }
  if (FB_THREAD_LOCAL(interception_recursion_depth) == 0
      && FB_THREAD_LOCAL(intercept_on) != NULL) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf),
             "Internal error while intercepting %s: already intercepting %s "
             "(and no signal or atfork handler running in this thread)",
             function_name, FB_THREAD_LOCAL(intercept_on));
    insert_debug_msg(debug_buf);
    assert(0 && "Internal error: nested interceptors (no signal handler running)");
  }

  if (!FB_THREAD_LOCAL(has_global_lock)) {
    pthread_mutex_lock(&ic_global_lock);
    FB_THREAD_LOCAL(has_global_lock) = true;
    FB_THREAD_LOCAL(intercept_on) = function_name;
    *i_locked = true;
  }
  thread_signal_danger_zone_leave();
  assert(FB_THREAD_LOCAL(signal_danger_zone_depth) == 0);
}

void release_global_lock() {
  thread_signal_danger_zone_enter();
  pthread_mutex_unlock(&ic_global_lock);
  FB_THREAD_LOCAL(has_global_lock) = false;
  FB_THREAD_LOCAL(intercept_on) = NULL;
  thread_signal_danger_zone_leave();
  assert(FB_THREAD_LOCAL(signal_danger_zone_depth) == 0);
}

/** debugging flags */
int32_t debug_flags = 0;

char env_ld_library_path[FB_PATH_BUFSIZE] = {0};

bool insert_trace_markers = false;

/** Next ACK id*/
static uint16_t ack_id = 1;

voidp_set popened_streams;

psfa *psfas = NULL;
int psfas_num = 0;
int psfas_alloc = 0;


void insert_debug_msg(const char* m) {
#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    int saved_errno = errno;
    char tpl[256] = "/FIREBUILD   ###   ";
    get_ic_orig_open()(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1), 0);
    errno = saved_errno;
  }
#else
  (void)m;
#endif
}

void insert_begin_marker(const char* m) {
  if (insert_trace_markers) {
    char tpl[256] = "intercept-begin: ";
    insert_debug_msg(strncat(tpl, m, sizeof(tpl) - strlen(tpl) - 1));
  }
}

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
static void fb_send_msg(int fd, const void /*FBBCOMM_Builder*/ *ic_msg, uint16_t ack_num) {
  int len = fbbcomm_builder_measure(ic_msg);
  char *buf = alloca(sizeof(msg_header) + len);
  memset(buf, 0, sizeof(msg_header));
  fbbcomm_builder_serialize(ic_msg, buf + sizeof(msg_header));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
  memset(buf, 0, sizeof(msg_header));
  ((msg_header *)buf)->ack_id = ack_num;
  ((msg_header *)buf)->msg_size = len;
#pragma GCC diagnostic pop
  fb_write(fd, buf, sizeof(msg_header) + len);
}

void fb_fbbcomm_send_msg(const void /*FBBCOMM_Builder*/ *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  fb_send_msg(fd, ic_msg, 0);

  thread_signal_danger_zone_leave();
}

uint16_t fb_fbbcomm_send_msg_with_ack(const void /*FBBCOMM_Builder*/ *ic_msg, int fd) {
  thread_signal_danger_zone_enter();

  uint16_t ack_num = get_next_ack_id();
  fb_send_msg(fd, ic_msg, ack_num);

  return ack_num;
}

void fb_fbbcomm_check_ack(int fd, uint16_t ack_num) {
#ifdef NDEBUG
  (void)ack_num;
#else
  uint16_t ack_num_resp =
#endif
      fb_recv_ack(fd);
  assert(ack_num_resp == ack_num);

  thread_signal_danger_zone_leave();
}

void fb_fbbcomm_send_msg_and_check_ack(const void /*FBBCOMM_Builder*/ *ic_msg, int fd) {
  uint16_t ack_num = fb_fbbcomm_send_msg_with_ack(ic_msg, fd);
  fb_fbbcomm_check_ack(fd, ack_num);
}

static void send_pre_open_internal(const int dirfd, const char* pathname, bool need_ack) {
  FBBCOMM_Builder_pre_open ic_msg;
  fbbcomm_builder_pre_open_init(&ic_msg);
  fbbcomm_builder_pre_open_set_dirfd(&ic_msg, dirfd);
  BUILDER_MAYBE_SET_ABSOLUTE_CANONICAL(pre_open, dirfd, pathname);
  if (need_ack) {
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  } else {
    fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
  }
}

void send_pre_open(const int dirfd, const char* pathname) {
  send_pre_open_internal(dirfd, pathname, true);
}

void send_pre_open_without_ack_request(const int dirfd, const char* pathname) {
  send_pre_open_internal(dirfd, pathname, false);
}

bool maybe_send_pre_open(const int dirfd, const char* pathname, int flags) {
  if (pathname && is_write(flags) && (flags & O_TRUNC)
      && !(flags & (O_EXCL | O_DIRECTORY))
#ifdef O_TMPFILE
      && (flags & O_TMPFILE) != O_TMPFILE
#endif
      && !is_path_at_locations(pathname, -1, &ignore_locations)) {
    send_pre_open(dirfd, pathname);
    return true;
  } else {
    return false;
  }
}

void pre_clone_disable_interception(const int flags, bool *i_locked) {
  FBBCOMM_Builder_clone ic_msg;
  fbbcomm_builder_clone_init(&ic_msg);

  /* Skipping 'fn' */
  /* Skipping 'stack' */
  fbbcomm_builder_clone_set_flags(&ic_msg, flags);
  /* Skipping 'arg' */
  /* Not sending return value */
  /* Send and go on, no ack */
  fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);

  /* clone() can be really tricky to intercept, for example when the cloned process shares
   * the file descriptor table with the parent (CLONE_FILES). In this case the interceptor
   * would have to protect two communication fds or implement locking across separate processes. */
  intercepting_enabled = false;
  env_purge(environ);
  /* Releasing the global lock (if we grabbed it in this pass) to not keep it locked
   *  in the forked process. */
  if (*i_locked) {
    release_global_lock();
    *i_locked = false;
  }
}

int clone_trampoline(void *arg) {
  clone_trampoline_arg *trampoline_arg = (clone_trampoline_arg *)arg;
  thread_signal_danger_zone_leave();
  if (trampoline_arg->i_locked) {
    release_global_lock();
  }
  atfork_child_handler();
  return(trampoline_arg->orig_fn(trampoline_arg->orig_arg));
}

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

/** Store from entries from environment variable. */
static void store_entries(const char* env_var, cstring_view_array *entries,
                          char * entries_env_buf, size_t buffer_size) {
  char* env_entries = getenv(env_var);
  if (env_entries) {
    strncpy(entries_env_buf, env_entries, buffer_size);
    const size_t env_entries_len = strlen(env_entries);
    if (env_entries_len + 1 > buffer_size) {
      /* Trim to the fitting parts. The entries are used only for improving
       * performance and the space is allocated statically. */
      entries_env_buf[buffer_size - 1] = '\0';
      char * last_separator = strrchr(entries_env_buf, ':');
      if (!last_separator) {
        /* This is a quite long single path that may be incomplete, thus ignore it. */
        entries_env_buf[0] = '\0';
      } else {
        /* Drop the possibly incomplete path after the last separator.*/
        *last_separator = '\0';
      }
    }
    char *prefix = entries_env_buf;
    /* Process all entries that fit location without reallocation. */
    while (prefix && !is_cstring_view_array_full(entries)) {
      char *next_prefix = strchr(prefix, ':');
      if (next_prefix) {
        *next_prefix = '\0';
        next_prefix++;
      }
      /* Skip "". */
      if (*prefix != '\0') {
        cstring_view_array_append_noalloc(entries, prefix);
        prefix = next_prefix;
      }
    }
  }
}

void newly_loaded_images(const char ** const images_before, const size_t image_count_before,
                         const char * const * images_after, size_t image_count_after,
                         const char ** new_images, size_t *new_images_count) {
  size_t first_differing_idx = 0;
  for (first_differing_idx = 0; first_differing_idx < image_count_before; first_differing_idx++) {
    if (strcmp(images_before[first_differing_idx], images_after[first_differing_idx]) !=  0) {
      break;
    }
  }
  qsort(images_before, image_count_before, sizeof(char*),
        (int (*)(const void *, const void *))strcmp);
  *new_images_count = 0;
  for (size_t i = first_differing_idx; i < image_count_after; i++) {
    if (!bsearch(&images_after[i], images_before, image_count_before, sizeof(char*),
                 (int (*)(const void *, const void *))strcmp)) {
      new_images[(*new_images_count)++] = images_after[i];
    }
  }
}

#ifdef __APPLE__
void collect_loaded_image_names(const char ** images, int image_count) {
  for (int i = 0; i < image_count; i++) {
    images[i] = _dyld_get_image_name(i);
  }
}

static void collect_canonized_shared_libs(cstring_view_array* libs, char *canonized_libs,
                                          int image_count) {
  /* Skip first image because it is the binary itself. */
  for (int32_t i = image_count - 1; i > 1 ; i--) {
    const char *image_name = _dyld_get_image_name(i);
    const size_t len = strlen(image_name);
    if (is_canonical(image_name, len)) {
      assert(!is_cstring_view_array_full(libs));
      cstring_view_array_append_noalloc(libs, (/* not const */ char *)image_name);
    } else {
      char *canonical_name = &canonized_libs[i * FB_PATH_BUFSIZE];
      memcpy(canonical_name, image_name, len + 1);
      make_canonical(canonical_name, len);
      assert(!is_cstring_view_array_full(libs));
      cstring_view_array_append_noalloc(libs, canonical_name);
    }
  }
}

#else
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
  if (strcmp(name, VDSO_NAME) == 0) {
    /* This is an in-kernel library, filter it out. */
    return true;
  }
  return false;
}

int count_shared_libs_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  (void)info;  /* unused */
  (void)size;  /* unused */
  int* count = (int*)data;
  (*count)++;
  return 0;
}

int shared_libs_as_char_array_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  (void) size;  /* unused */
  shared_libs_as_char_array_cb_data_t *cb_data =
      (shared_libs_as_char_array_cb_data_t *)data;

  cb_data->array[cb_data->collected_entries] = info->dlpi_name;
  cb_data->collected_entries++;
  assert(cb_data->collected_entries <= cb_data->collectable_entries);
  return 0;
}

/**
 * State struct for shared_libs_as_cstring_view_array_cb()
 */
typedef struct shared_libs_as_cstring_view_array_cb_data_ {
  /** Array of collected shared library names. */
  cstring_view_array *array;
  /** Number of entries that could be collected to `array`. */
  int collectable_entries;
  /** Number of entries that are not in canonical form, thus need to be made canonical. */
  int not_canonical_entries;
  /** Buffert to store canonized library names. Size is canonized_libs_size * FB_PATH_BUFSIZE. */
  char *canonized_libs;
  /** Number of canonized names canonized_libs can store. */
  int canonized_libs_size;
  /** Number of canonized names stored in canonized_libs. */
  int canonized_libs_count;
} shared_libs_as_cstring_view_array_cb_data_t;

/** Add shared library's name to the file list */
static int shared_libs_as_cstring_view_array_cb(struct dl_phdr_info *info, const size_t size,
                                                void *data) {
  (void) size;  /* unused */
  shared_libs_as_cstring_view_array_cb_data_t *cb_data =
      (shared_libs_as_cstring_view_array_cb_data_t *)data;
  cstring_view_array *array = cb_data->array;

  const char* name = info->dlpi_name;
  const size_t len = strlen(name);
  if (skip_shared_lib(name, len)) {
    return 0;
  }
  cb_data->collectable_entries++;
  if (is_canonical(name, len)) {
    if (!is_cstring_view_array_full(array)) {
      cstring_view_array_append_noalloc(array, (/* non-const */ char *) name);
    }
  } else {
    /* !is_canonical() */
    cb_data->not_canonical_entries++;
    assert(cb_data->canonized_libs_count <= cb_data->canonized_libs_size);
    if (cb_data->canonized_libs_count < cb_data->canonized_libs_size) {
      /* The there is enough space for the new canonized entry. */
      char * canonical_name =
          &cb_data->canonized_libs[cb_data->canonized_libs_count++ * FB_PATH_BUFSIZE];
      memcpy(canonical_name, name, len + 1);
      make_canonical(canonical_name, len);
      if (!is_cstring_view_array_full(array)) {
        cstring_view_array_append_noalloc(array, canonical_name);
      }
    }
  }
  return 0;
}
#endif

void atfork_parent_handler(void) {
  /* The variable i_am_intercepting from the intercepted fork() is
   * not available here, and storing it in a thread-global variable is
   * probably not worth the trouble. */
  if (intercepting_enabled) {
    FBBCOMM_Builder_fork_parent ic_msg;
    fbbcomm_builder_fork_parent_init(&ic_msg);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
}

void atfork_child_handler(void) {
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
    ic_pid = get_ic_orig_getpid()();

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
    if (!FB_THREAD_LOCAL(has_global_lock)) {
      pthread_mutex_lock(&ic_global_lock);
      FB_THREAD_LOCAL(has_global_lock) = true;
      FB_THREAD_LOCAL(intercept_on) = "handle_exit";
      i_locked = true;
    }
    thread_signal_danger_zone_leave();

    FBBCOMM_Builder_rusage ic_msg;
    fbbcomm_builder_rusage_init(&ic_msg);

    struct rusage ru;
    get_ic_orig_getrusage()(RUSAGE_SELF, &ru);
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
      FB_THREAD_LOCAL(has_global_lock) = false;
      FB_THREAD_LOCAL(intercept_on) = NULL;
      thread_signal_danger_zone_leave();
    }
  }
}

void *pthread_start_routine_wrapper(void *routine_and_arg) {
  if (insert_trace_markers) {
    char buf[256];
    snprintf(buf, sizeof(buf), "launched via pthread_create() in pid %d", get_ic_orig_getpid()());
    insert_debug_msg(buf);
  }
  void *(*start_routine)(void *) = ((void **)routine_and_arg)[0];
  void *arg = ((void **)routine_and_arg)[1];
  free(routine_and_arg);
  return (*start_routine)(arg);
}

/**
 * Parses and returns GNU Make jobserver fds if they are present in makeflags.
 * e.g. --jobserver-auth=R,W where ‘R’ and ‘W’ are non-negative integers representing fds
 *
 * @param[in] makeflags Make flags as set in environment's MAKEFLAGS
 * @param[out] fd_r R fd
 * @param[out] fd_w W fd
 * @return true, if jobserver fds were set and parsed
 */
static bool extract_jobserver_fds(const char* makeflags_env, int *fd_r, int *fd_w) {
  const char *makeflags = getenv(makeflags_env);
  if (!makeflags) {
    return false;
  }
  const char *needle = "--jobserver-auth=";
  const char *jobserver_option = strstr(makeflags, needle);
  if (!jobserver_option) {
    needle = "--jobserver-fds=";
    jobserver_option = strstr(makeflags, needle);
  }
  if (jobserver_option) {
    if (sscanf(jobserver_option + strlen(needle), "%d,%d", fd_r, fd_w) == 2) {
      return true;
    }
  }
  return false;
}


/**
 * Set up a supervisor connection
 * @return fd of the connection
 */
int fb_connect_supervisor() {
#ifdef SOCK_CLOEXEC
  int conn = TEMP_FAILURE_RETRY(get_ic_orig_socket()(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
#else
  int conn = TEMP_FAILURE_RETRY(get_ic_orig_socket()(AF_UNIX, SOCK_STREAM, 0));
#ifndef NDEBUG
  int fcntl_ret =
#endif
      TEMP_FAILURE_RETRY(get_ic_orig_fcntl()(conn, F_SETFD, FD_CLOEXEC));
  assert(fcntl_ret != -1);
#endif
  assert(conn != -1);

  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(remote));
  remote.sun_family = AF_UNIX;
#ifdef FB_EXTRA_DEBUG
  assert(strlen(fb_conn_string) < sizeof(remote.sun_path));
#endif
  strncpy(remote.sun_path, fb_conn_string, sizeof(remote.sun_path));

  int conn_ret = TEMP_FAILURE_RETRY(
      get_ic_orig_connect()(conn, (struct sockaddr *)&remote, sizeof(remote)));
  if (conn_ret == -1) {
    get_ic_orig_perror()("connect");
    assert(0 && "connection to supervisor failed");
  }
  return conn;
}

void fb_init_supervisor_conn() {
  if (fb_conn_string[0] == '\0') {
    strncpy(fb_conn_string, getenv("FB_SOCKET"), sizeof(fb_conn_string));
    fb_conn_string_len = strlen(fb_conn_string);
  }
  /* Reconnect to supervisor.
   * POSIX says to retry close() on EINTR (e.g. wrap in TEMP_FAILURE_RETRY())
   * but Linux probably disagrees, see #723. */
  get_ic_orig_close()(fb_sv_conn);
  fb_sv_conn = fb_connect_supervisor();
}

/**
 * Detect main()'s argc and argv with heuristics.
 *
 * The reliable and portable initialization happens in fb_ic_init_constructor(), but in case
 * an intercepted function is called before the constructor of this shared library, argc
 * and argv still needs to be reported to the supervisor in the first message.
 * The heuristics below works for most programs, but not for mpicc. Luckily when intercepting
 * mpicc the constructor is called first, thus this heuristics is not used.
 */
static void init_argc_argv() {
#ifdef __APPLE__
  char** __environ = environ;
#endif
  if (ic_argv == NULL) {
    char* arg = *(__environ - 2);
    unsigned long int argc_guess = 0;

    /* argv is NULL terminated */
    assert(*(__environ - 1) == NULL);
    /* walk back on argv[] to find the first value matching the counted argument number */
    while (argc_guess != (unsigned long int)arg) {
      argc_guess++;
      arg = *(__environ - 2 - argc_guess);
    }
    ic_argc = argc_guess;
    ic_argv = __environ - 1 - argc_guess;
  }
}

/**
 * Initialize interceptor's data structures and sync with supervisor.
 *
 * Collect information about process the earliest possible, right
 * when interceptor library loads or when the first interceped call happens
 */
void fb_ic_init() {
  /* Run only once, at startup. */
  if (ic_init_started) {
    /* Should not be called recursively. */
    assert(ic_init_done);
    return;
  }
  ic_init_started = true;
  get_ic_orig_getrusage()(RUSAGE_SELF, &initial_rusage);

  if (getenv("FB_INSERT_TRACE_MARKERS") != NULL) {
    insert_trace_markers = true;
  }

  store_entries("FB_READ_ONLY_LOCATIONS", &read_only_locations, read_only_locations_env_buf,
                sizeof(read_only_locations_env_buf));
  store_entries("FB_IGNORE_LOCATIONS", &ignore_locations, ignore_locations_env_buf,
                sizeof(ignore_locations_env_buf));
  store_entries("FB_JOBSERVER_USERS", &jobserver_users, jobserver_users_env_buf,
                sizeof(jobserver_users_env_buf));

#ifndef __mips__
  /* We use an uint64_t as bitmap for delayed signals. Make sure it's okay.
   * On MIPS it is not enough, signals > 64 will not be wrapped. */
  assert(SIGRTMAX <= IC_WRAP_SIGRTMAX);
#endif

  voidp_set_init(&popened_streams);

  reset_interceptors();

  assert(FB_THREAD_LOCAL(intercept_on) == NULL);
  FB_THREAD_LOCAL(intercept_on) = "init";
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

  init_argc_argv();

  pid_t pid, ppid;
  ic_pid = pid = get_ic_orig_getpid()();
  ppid = get_ic_orig_getppid()();

  if (get_ic_orig_getcwd()(ic_cwd, sizeof(ic_cwd)) == NULL) {
    assert(0 && "getcwd() returned NULL");
  }
  ic_cwd_len = strlen(ic_cwd);

  FBBCOMM_Builder_scproc_query ic_msg;
  fbbcomm_builder_scproc_query_init(&ic_msg);

  fbbcomm_builder_scproc_query_set_version(&ic_msg, FIREBUILD_VERSION);

  fbbcomm_builder_scproc_query_set_pid(&ic_msg, pid);
  fbbcomm_builder_scproc_query_set_ppid(&ic_msg, ppid);
  fbbcomm_builder_scproc_query_set_cwd(&ic_msg, ic_cwd);
  fbbcomm_builder_scproc_query_set_arg_with_count(&ic_msg, (const char **) ic_argv, ic_argc);

  mode_t initial_umask = get_ic_orig_umask()(0077);
  get_ic_orig_umask()(initial_umask);
  fbbcomm_builder_scproc_query_set_umask(&ic_msg, initial_umask);

  /* make a sorted and filtered copy of env */
#ifdef __APPLE__
  char **env = environ;
#else
  char **env = __environ;
#endif
  int env_len = 0, env_copy_len = 0;
  for (char** cursor = env; *cursor != NULL; cursor++) {
    env_len++;
  }
  char *env_copy[sizeof(env[0]) * (env_len + 1)];

  for (char** cursor = env; *cursor != NULL; cursor++) {
    const char *fb_socket = "FB_SOCKET=";
    const char *fb_read_only_locations = "FB_READ_ONLY_LOCATIONS=";
    const char *fb_ignore_locations = "FB_IGNORE_LOCATIONS=";
    const char *fb_jobserver_users = "FB_JOBSERVER_USERS=";
    if (strncmp(*cursor, fb_socket, strlen(fb_socket)) != 0 &&
        strncmp(*cursor, fb_read_only_locations, strlen(fb_read_only_locations)) != 0 &&
        strncmp(*cursor, fb_ignore_locations, strlen(fb_ignore_locations)) != 0 &&
        strncmp(*cursor, fb_jobserver_users, strlen(fb_jobserver_users)) != 0) {
      env_copy[env_copy_len++] = *cursor;
    }
  }
  env_copy[env_copy_len] = NULL;
  qsort(env_copy, env_copy_len, sizeof(env_copy[0]), cmpstringpp);
  fbbcomm_builder_scproc_query_set_env_var(&ic_msg, (const char **) env_copy);

  const char* slash_pos = strrchr(ic_argv[0], '/');
  const char* cmd_name = slash_pos ? slash_pos + 1 : ic_argv[0];
  int jobserver_fds[] = {-1, -1};
  if (is_in_sorted_cstring_view_array(cmd_name, strlen(cmd_name), &jobserver_users)) {
    if (extract_jobserver_fds("CARGO_MAKEFLAGS", &jobserver_fds[0], &jobserver_fds[1])) {
      fbbcomm_builder_scproc_query_set_jobserver_fds(&ic_msg, jobserver_fds, 2);
    } else if (extract_jobserver_fds("MAKEFLAGS", &jobserver_fds[0], &jobserver_fds[1])) {
      fbbcomm_builder_scproc_query_set_jobserver_fds(&ic_msg, jobserver_fds, 2);
    }
  }

  /* get full executable path
   * see http://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe
   * and readlink(2) */
  char linkname[FB_PATH_BUFSIZE];
#ifdef __APPLE__
  uint32_t r = sizeof(linkname);
  if (_NSGetExecutablePath(linkname, &r) == 0) {
    r = strlen(linkname);
  } else {
    /* A bigger buffer is needed. */
    char* linkname2 = alloca(++r);
    if (_NSGetExecutablePath(linkname2, &r) != 0) {
      assert(0 && "Could not get the executable path even with the buffer "
             "that should have been enough.");
      r = 0;
    } else {
      r = strlen(linkname2);
      fbbcomm_builder_scproc_query_set_executable_with_length(&ic_msg, linkname2, r);
    }
  }
#else
  ssize_t r = get_ic_orig_readlink()("/proc/self/exe", linkname, FB_PATH_BUFSIZE - 1);
#endif
  if (r > 0 && r < FB_PATH_BUFSIZE) {
    linkname[r] = '\0';
    fbbcomm_builder_scproc_query_set_executable_with_length(&ic_msg, linkname, r);
  }

#ifdef __APPLE__
  char original_executed_path[PROC_PIDPATHINFO_MAXSIZE];
  if (proc_pidpath(getpid(), original_executed_path, sizeof(original_executed_path)) != -1
#else
  const char *original_executed_path = (const char*)getauxval(AT_EXECFN);
  if (original_executed_path
#endif
      && strcmp(original_executed_path, linkname) != 0) {
    /* The macro relies on the field name matching the variable name. */
    const char *executed_path = original_executed_path;
    BUILDER_SET_ABSOLUTE_CANONICAL(scproc_query, executed_path);
    if (strcmp(fbbcomm_builder_scproc_query_get_executed_path(&ic_msg),
               original_executed_path) != 0) {
      fbbcomm_builder_scproc_query_set_original_executed_path(&ic_msg, original_executed_path);
    }
  }

  /* list loaded shared libs */
#ifdef __APPLE__
  const int image_count = _dyld_image_count();
  cstring_view *libs_ptrs = alloca((image_count + 1) * sizeof(cstring_view));
  cstring_view_array libs = {libs_ptrs, 0, image_count + 1};
  char *canonized_libs = alloca(image_count * FB_PATH_BUFSIZE);
  collect_canonized_shared_libs(&libs, canonized_libs, image_count);
#else
  STATIC_CSTRING_VIEW_ARRAY(libs, 64);
  int canonized_libs_size = 8;
  char *canonized_libs = alloca(canonized_libs_size * FB_PATH_BUFSIZE);
  shared_libs_as_cstring_view_array_cb_data_t cb_data =
      {&libs, 0, 0, canonized_libs, canonized_libs_size, 0};
  dl_iterate_phdr(shared_libs_as_cstring_view_array_cb, &cb_data);
  if (cb_data.collectable_entries > cb_data.array->len) {
    if (cb_data.not_canonical_entries > canonized_libs_size) {
      /* canonized_libs was not big enough. */
      canonized_libs_size = cb_data.not_canonical_entries;
      canonized_libs = alloca(canonized_libs_size * FB_PATH_BUFSIZE);
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

    shared_libs_as_cstring_view_array_cb_data_t cb_data2 =
        {&libs, 0, 0, canonized_libs, canonized_libs_size, 0};
    dl_iterate_phdr(shared_libs_as_cstring_view_array_cb, &cb_data2);
    assert(cb_data.collectable_entries == cb_data2.array->len);
  }
#endif
  fbbcomm_builder_scproc_query_set_libs_cstring_views(&ic_msg, libs.p, libs.len);

  fb_send_msg(fb_sv_conn, &ic_msg, 0);

  /* Read the scproc_resp message header. */
  msg_header header;
#ifndef NDEBUG
  ssize_t ret =
#endif
      fb_read(fb_sv_conn, &header, sizeof(header));
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
#ifndef NDEBUG
  ret =
#endif
      TEMP_FAILURE_RETRY(get_ic_orig_recvmsg()(fb_sv_conn, &msgh, 0));
  assert(ret >= 0 && ret == (ssize_t)header.msg_size);
  assert(fbbcomm_serialized_get_tag(sv_msg_generic) == FBBCOMM_TAG_scproc_resp);

  FBBCOMM_Serialized_scproc_resp *sv_msg = (FBBCOMM_Serialized_scproc_resp *) sv_msg_generic;
  debug_flags = fbbcomm_serialized_scproc_resp_get_debug_flags_with_fallback(sv_msg, 0);

  /* we may return immediately if supervisor decides that way */
  if (fbbcomm_serialized_scproc_resp_get_shortcut(sv_msg)) {
    insert_debug_msg("this process was shortcut by the supervisor");

    for (fbb_size_t i = 0; i < fbbcomm_serialized_scproc_resp_get_fds_appended_to_count(sv_msg);
         i++) {
      int fd = fbbcomm_serialized_scproc_resp_get_fds_appended_to_at(sv_msg, i);
      insert_debug_msg("seeking forward in fd");
      get_ic_orig_lseek()(fd, 0, SEEK_END);
    }

    insert_debug_msg("exiting");
#ifdef __APPLE__
    _exit(fbbcomm_serialized_scproc_resp_get_exit_status(sv_msg));
#else
    void(*orig_underscore_exit)(int) = (void(*)(int)) dlsym(RTLD_NEXT, "_exit");
    (*orig_underscore_exit)(fbbcomm_serialized_scproc_resp_get_exit_status(sv_msg));
#endif
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
          get_ic_orig_fcntl()(fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_at(fds, 0), F_GETFL);
      assert(flags != -1);
#ifndef NDEBUG
      int fcntl_ret =
#endif
          get_ic_orig_fcntl()(src_fd, F_SETFL, flags);
      assert(fcntl_ret != -1);

      /* Dup2 the source fd to the desired places and then close the original. */
      for (size_t j = 0; j < fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_count(fds); j++) {
        int dst_fd = fbbcomm_serialized_scproc_resp_reopen_fd_get_fds_at(fds, j);
#ifndef NDEBUG
        int dup2_ret =
#endif
            get_ic_orig_dup2()(src_fd, dst_fd);
        assert(dup2_ret == dst_fd);
      }
      get_ic_orig_close()(src_fd);
    }
  }

  /* Report back each inherited fd not seeked to the end. */
  for (fbb_size_t i = 0; i < fbbcomm_serialized_scproc_resp_get_seekable_fds_count(sv_msg);
       i++) {
    int fd = fbbcomm_serialized_scproc_resp_get_seekable_fds_at(sv_msg, i);
    int64_t size = fbbcomm_serialized_scproc_resp_get_seekable_fds_size_at(sv_msg, i);
    insert_debug_msg("get offset of fd");
#ifdef __APPLE__
    off_t offset = get_ic_orig_lseek()(fd, 0, SEEK_CUR);
#else
    off64_t offset = get_ic_orig_lseek64()(fd, 0, SEEK_CUR);
#endif
    if (offset != size) {
      FBBCOMM_Builder_inherited_fd_offset ic_msg;
      fbbcomm_builder_inherited_fd_offset_init(&ic_msg);
      fbbcomm_builder_inherited_fd_offset_set_fd(&ic_msg, fd);
      fbbcomm_builder_inherited_fd_offset_set_offset(&ic_msg, offset);
      fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
    }
  }

  insert_debug_msg("initialization-end");
  FB_THREAD_LOCAL(intercept_on) = NULL;
  ic_init_done = true;
}

static void fb_ic_init_constructor(int argc, char **argv) {
  if (!ic_init_started) {
    ic_argc = argc;
    ic_argv = argv;
    fb_ic_init();
  }
}

static void fb_ic_cleanup() {
  /* Don't put anything here, unless you really know what you're doing!
   * Our atexit_handler, which reports the resource usage to the supervisor,
   * is run _after_ this destructor, and still needs pretty much all the
   * functionality that we have (including the communication channel). */
}


ssize_t fb_read(int fd, void *buf, size_t count) {
  FB_READ_WRITE(*get_ic_orig_read(), fd, buf, count);
}

ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(*get_ic_orig_write(), fd, buf, count);
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
  /* For addopen() and addchdir_np() actions the filename needs to be freed. */
  if (fbbcomm_builder_get_tag(p) == FBBCOMM_TAG_posix_spawn_file_action_open) {
    FBBCOMM_Builder_posix_spawn_file_action_open *builder = p;
    char *pathname =
        (/* non-const */ char *)fbbcomm_builder_posix_spawn_file_action_open_get_pathname(builder);
    free(pathname);
  } else if (fbbcomm_builder_get_tag(p) == FBBCOMM_TAG_posix_spawn_file_action_chdir) {
    FBBCOMM_Builder_posix_spawn_file_action_chdir *builder = p;
    char *pathname =
        (/* non-const */ char *)fbbcomm_builder_posix_spawn_file_action_chdir_get_pathname(builder);
    free(pathname);
  }
  free(p);
}

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

void psfa_addopen(const posix_spawn_file_actions_t *p,
                  int fd,
                  const char *pathname,
                  int flags,
                  mode_t mode) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_open *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_open));
  fbbcomm_builder_posix_spawn_file_action_open_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_open_set_fd(fbbcomm_builder, fd);
  fbbcomm_builder_posix_spawn_file_action_open_set_pathname(fbbcomm_builder, strdup(pathname));
  fbbcomm_builder_posix_spawn_file_action_open_set_flags(fbbcomm_builder, flags);
  fbbcomm_builder_posix_spawn_file_action_open_set_mode(fbbcomm_builder, mode);

  voidp_array_append(obj, fbbcomm_builder);
}

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

void psfa_addclosefrom_np(const posix_spawn_file_actions_t *p,
                          int lowfd) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_closefrom *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_closefrom));
  fbbcomm_builder_posix_spawn_file_action_closefrom_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_closefrom_set_lowfd(fbbcomm_builder, lowfd);

  voidp_array_append(obj, fbbcomm_builder);
}

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

void psfa_addchdir_np(const posix_spawn_file_actions_t *p,
                      const char *pathname) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_chdir *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_chdir));
  fbbcomm_builder_posix_spawn_file_action_chdir_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_chdir_set_pathname(fbbcomm_builder, strdup(pathname));

  voidp_array_append(obj, fbbcomm_builder);
}

void psfa_addfchdir_np(const posix_spawn_file_actions_t *p,
                       int fd) {
  voidp_array *obj = psfa_find(p);
  assert(obj);

  FBBCOMM_Builder_posix_spawn_file_action_fchdir *fbbcomm_builder =
      malloc(sizeof(FBBCOMM_Builder_posix_spawn_file_action_fchdir));
  fbbcomm_builder_posix_spawn_file_action_fchdir_init(fbbcomm_builder);

  fbbcomm_builder_posix_spawn_file_action_fchdir_set_fd(fbbcomm_builder, fd);

  voidp_array_append(obj, fbbcomm_builder);
}

voidp_array *psfa_find(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      return &psfas[i].actions;
    }
  }
  return NULL;
}
