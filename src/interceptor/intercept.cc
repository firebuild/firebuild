/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "interceptor/intercept.h"

#include <unistd.h>
#include <errno.h>
#include <link.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <spawn.h>

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <string>

#include "interceptor/env.h"
#include "fb-messages.pb.h"
#include "common/firebuild_common.h"

namespace firebuild {

#ifdef  __cplusplus
extern "C" {
#endif

static void fb_ic_cleanup() __attribute__((destructor));

#ifdef  __cplusplus
}
#endif

/* global vars */
ic_fn_info ic_fn[IC_FN_IDX_MAX];

/** file fd states */
std::vector<fd_state> *fd_states;

/** Global lock for manipulating fd states */
pthread_mutex_t ic_fd_states_lock;

/* local declarations for original intercepted functions */
#undef IC_VOID
/* create ic_orig_... version of intercepted function */
#define IC_VOID(ret_type, name, parameters, _body)  \
  ret_type(*ic_orig_##name) parameters = NULL;

/* we need to include every file using IC() macro to create ic_orig_... version
 * for all functions */
#include "interceptor/ic_file_ops.h"

#undef IC_VOID

/** Global lock for serializing critical interceptor actions */
pthread_mutex_t ic_global_lock = PTHREAD_MUTEX_INITIALIZER;

/** Connection string to supervisor */
char * fb_conn_string = NULL;

/** Connection file descriptor to supervisor */
int fb_sv_conn = -1;

/** interceptor init has been run */
bool ic_init_done = false;

/**
 * Stored PID
 * When getpid() returns a different value, we missed a fork() :-)
 */
int ic_pid;

/** Per thread variable which we turn on inside call interception */
__thread bool intercept_on = false;

/** debugging flags */
int32_t debug_flags = 0;

/** Insert marker open()-s for strace, ltrace, etc. */
static bool insert_trace_markers = false;

/** Next ACK id*/
static int ack_id = 0;

/** Insert debug message */
void insert_debug_msg(const std::string &m) {
  if (insert_trace_markers) {
    int saved_errno = errno;
    const std::string tpl = "/FIREBUILD   ###   ";
    if (ic_orig_open) {
      ic_orig_open((tpl + m).c_str(), 0);
    } else {
      auto orig_open = (int(*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
      assert(orig_open);
      orig_open((tpl + m).c_str(), 0);
    }
    errno = saved_errno;
  }
}

/** Insert interception begin marker */
void insert_begin_marker(const std::string &m) {
  if (insert_trace_markers) {
    // TODO(egmont) Can string concatenation tamper with errno? Let's play safe.
    int saved_errno = errno;
    insert_debug_msg("intercept-begin: " + m);
    errno = saved_errno;
  }
}

/** Insert interception end marker */
void insert_end_marker(const std::string &m) {
  if (insert_trace_markers) {
    // TODO(egmont) Can string concatenation tamper with errno? Let's play safe.
    int saved_errno = errno;
    insert_debug_msg("intercept-end: " + m);
    errno = saved_errno;
  }
}

/**
 * Reset globally maintained information about intercepted functions
 */
void reset_fn_infos() {
  for (int i = 0; i < IC_FN_IDX_MAX ; i++) {
    ic_fn[i].called = false;
  }
}

/**
 * Get pointer to a function implemented in the next shared
 * library. In our case this is a function we intercept.
 * @param[in] name function's name
 */
static void * get_orig_fn(const char* name) {
  void * const function = dlsym(RTLD_NEXT, name);
  return function;
}

/** Get next unique ACK id */
int get_next_ack_id() {
  return (ack_id++);
}

/**
 * Get pointers to all the functions we intercept but we also want to use
 */
static void set_orig_fns() {
  /* lookup ic_orig_... version of intercepted function */
#define IC_VOID(ret_type, name, parameters, _body)              \
  ic_orig_##name = (ret_type(*)parameters)get_orig_fn(#name);

  /* we need to include every file using IC() macro to create ic_orig_... version
   * for all functions */
#include "interceptor/ic_file_ops.h"

#undef IC_VOID
}

/**  Set up supervisor connection */
void init_supervisor_conn() {
  if (fb_conn_string == NULL) {
    fb_conn_string = strdup(getenv("FB_SOCKET"));
  }

  if (-1 == (fb_sv_conn =
             ic_orig_socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0))) {
    assert(fb_sv_conn > STDERR_FILENO);
    assert(fb_sv_conn != -1);
  }

  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(remote));
  remote.sun_family = AF_UNIX;
  assert(strlen(fb_conn_string) + 1 < sizeof(remote.sun_path));
  /* always use the first socket from the pool for the first connection */
  snprintf(remote.sun_path, sizeof(remote.sun_path), "%s%d", fb_conn_string, 0);

  if (-1 == ic_orig_connect(fb_sv_conn,
                            (struct sockaddr *)&remote, sizeof(remote))) {
    perror("connect");
    assert(0 && "connection to supervisor failed");
  }
}

/**
 * Initialize interceptor's data structures and sync with supervisor
 */
static void fb_ic_init() {
  // init global variables
  fd_states = new std::vector<fd_state>();

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  set_orig_fns();
  reset_fn_infos();

  intercept_on = true;
  insert_begin_marker(__func__);

  if (NULL != getenv("FB_INSERT_TRACE_MARKERS")) {
    insert_trace_markers = true;
  }

  init_supervisor_conn();

  on_exit(on_exit_handler, NULL);

  char **argv, **env;
  get_argv_env(&argv, &env);

  pid_t pid, ppid;
  ic_pid = pid = ic_orig_getpid();
  ppid = ic_orig_getppid();

  char cwd_buf[CWD_BUFSIZE];
  auto cwd_ret = ic_orig_getcwd(cwd_buf, CWD_BUFSIZE);
  assert(cwd_ret != NULL);

  msg::InterceptorMsg ic_msg;
  auto proc = ic_msg.mutable_scproc_query();

  proc->set_pid(pid);
  proc->set_ppid(ppid);
  proc->set_cwd(cwd_buf);

  for (auto cursor = argv; *cursor != NULL; cursor++) {
    proc->add_arg(*cursor);
  }

  for (auto cursor = env; *cursor != NULL; cursor++) {
    if (strncmp(*cursor, "FB_SOCKET=", strlen("FB_SOCKET=")) != 0) {
      proc->add_env_var(*cursor);
    }
  }
  std::sort(proc->mutable_env_var()->begin(),
            proc->mutable_env_var()->end());

  // get full executable path
  // see http://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe
  // and man 2 readlink
  {
    char linkname[CWD_BUFSIZE];
    ssize_t r;
    r = ic_orig_readlink("/proc/self/exe", linkname, CWD_BUFSIZE - 1);

    if ((r < 0) || (r > CWD_BUFSIZE - 1)) {
      // skip
      goto exec_path_filled;
    }

    linkname[r] = '\0';
    proc->set_executable(linkname);
  }
 exec_path_filled:

  // list loaded shared libs
  {
    msg::FileList *fl = proc->mutable_libs();
    dl_iterate_phdr(shared_libs_cb, fl);
  }

  fb_send_msg(ic_msg, fb_sv_conn);

  msg::SupervisorMsg sv_msg;
  fb_recv_msg(&sv_msg, fb_sv_conn);

  auto resp = sv_msg.mutable_scproc_resp();
  // we may return immediately if supervisor decides that way
  if (resp->shortcut()) {
    assert(resp->has_exit_status());
    exit(resp->exit_status());
  } else {
    if (resp->has_debug_flags()) {
      debug_flags = resp->debug_flags();
    }
  }
  ic_init_done = true;
  insert_end_marker(__func__);
  intercept_on = false;
}

extern "C" {
/**
 * Collect information about process the earliest possible, right
 * when interceptor library loads or when the first interceped call happens
 */
void fb_ic_load() {
  if (!ic_init_done) {
    fb_ic_init();
  }
}

void on_exit_handler(const int status, void *) {
  insert_debug_msg("our_on_exit_handler-begin");
  handle_exit(status);
  insert_debug_msg("our_on_exit_handler-end");

  /* Destruct global stuff here so that valgrind doesn't complain.
   * This is the last place anything from the interceptor code can be
   * executed. Alas it's skipped if one does an _exit(). */
  google::protobuf::ShutdownProtobufLibrary();
  ic_orig_close(fb_sv_conn);
  free(fb_conn_string);
  delete fd_states;
}

void handle_exit(const int status) {
  /* On rare occasions (e.g. two threads attempting to exit at the same
   * time) this method is called multiple times. The server can safely
   * handle it. */

  msg::InterceptorMsg ic_msg;
  auto m = ic_msg.mutable_exit();
  m->set_exit_status(status);

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  m->set_utime_u((int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
  m->set_stime_u((int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);
  {
    auto *fl = m->mutable_libs();
    dl_iterate_phdr(shared_libs_cb, fl);
  }
  int ack_num = get_next_ack_id();
  ic_msg.set_ack_num(ack_num);
  fb_send_msg(ic_msg, fb_sv_conn);
  msg::SupervisorMsg sv_msg;
  auto len = fb_recv_msg(&sv_msg, fb_sv_conn);
  if ((len > 0) && (sv_msg.ack_num() != ack_num)) {
    // something unexpected happened ...
    assert(0 && "Supervisor did not ack exit");
  }
}
}

static void fb_ic_cleanup() {
  /* Don't put anything here, unless you really know what you're doing!
   * Our on_exit_handler, which reports the exit code and resource usage
   * to the supervisor, is run _after_ this destructor, and still needs
   * pretty much all the functionality that we have (including the
   * communication channel and the protobuf stuff). */
}


/** wrapper for send() retrying on recoverable errors*/
ssize_t fb_write_buf(const int fd, const void * const buf, const size_t count) {
  pthread_mutex_lock(&ic_global_lock);
  FB_IO_OP_BUF(ic_orig_send, fd, buf, count, 0, {
      pthread_mutex_unlock(&ic_global_lock);});
}

/** wrapper for recv() retrying on recoverable errors*/
ssize_t fb_read_buf(const int fd,  void * const buf, const size_t count) {
  pthread_mutex_lock(&ic_global_lock);
  FB_IO_OP_BUF(ic_orig_recv, fd, buf, count, 0, {
      pthread_mutex_unlock(&ic_global_lock);});
}

/** Send error message to supervisor */
extern void fb_error(const std::string &msg) {
  msg::InterceptorMsg ic_msg;
  auto err = ic_msg.mutable_fb_error();
  err->set_msg(msg);
  fb_send_msg(ic_msg, fb_sv_conn);
}

/** Send debug message to supervisor if debug level is at least lvl */
void fb_debug(const std::string &msg) {
  msg::InterceptorMsg ic_msg;
  auto dbg = ic_msg.mutable_fb_debug();
  dbg->set_msg(msg);
  fb_send_msg(ic_msg, fb_sv_conn);
}

}  // namespace firebuild

/** Add shared library's name to the file list */
int shared_libs_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  auto *fl = (firebuild::msg::FileList*)data;
  // unused
  (void)size;

  if (info->dlpi_name[0] != '\0') {
    fl->add_file(info->dlpi_name);
  }

  return 0;
}

/* make auditing functions visible */
#pragma GCC visibility push(default)

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Dynamic linker auditing function
 * see man rtld-audit(7) for details
 */
unsigned int la_version(const unsigned int version) {
  return version;
}

/**
 * Send path to supervisor whenever the dynamic linker wants to load a shared
 * library
 */
char * la_objsearch(const char *name, uintptr_t *cookie,
                    const unsigned int flag) {
  // unused
  (void)cookie;

  fb_ic_load();

  firebuild::msg::InterceptorMsg ic_msg;
  auto *los = ic_msg.mutable_la_objsearch();
  los->set_name(name);
  los->set_flag(flag);
  firebuild::fb_send_msg(ic_msg, firebuild::fb_sv_conn);

  return const_cast<char*>(name);
}

/**
 * Send path to supervisor whenever the dynamic linker loads a shared library
 */
unsigned int la_objopen(struct link_map *map, const Lmid_t lmid,
                        uintptr_t *cookie) {
  // unused
  (void)lmid;
  (void)cookie;

  fb_ic_load();

  firebuild::msg::InterceptorMsg ic_msg;
  auto *los = ic_msg.mutable_la_objopen();
  los->set_name(map->l_name);
  firebuild::fb_send_msg(ic_msg, firebuild::fb_sv_conn);

  return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}


#ifdef  __cplusplus
}
#endif

#pragma GCC visibility pop

