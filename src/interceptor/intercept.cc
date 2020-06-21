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

#include <cassert>
#include <cstdarg>
#include <cstdlib>

#include "interceptor/env.h"
#include "interceptor/interceptors.h"
#include "fb-messages.pb.h"
#include "common/firebuild_common.h"

namespace firebuild {

extern "C" {

/** A poor man's (plain C) implementation of a hashmap:
 *  posix_spawn_file_actions_t -> msg::PosixSpawnFileActions
 *  implemented as a dense array with linear lookup.
 */
typedef struct {
  const posix_spawn_file_actions_t *p;
  msg::PosixSpawnFileActions *protobuf;
} psfa;
extern psfa *psfas;
extern int psfas_num;
extern int psfas_alloc;

static void fb_ic_cleanup() __attribute__((destructor));

/** file fd states */
fd_state ic_fd_states[IC_FD_STATES_SIZE];

/** Global lock for manipulating fd states */
pthread_mutex_t ic_fd_states_lock;

/** Global lock for preventing parallel system and popen calls */
pthread_mutex_t ic_system_popen_lock;

/** Global lock for serializing critical interceptor actions */
pthread_mutex_t ic_global_lock = PTHREAD_MUTEX_INITIALIZER;

/** Connection string to supervisor */
char * fb_conn_string = NULL;

/** Connection file descriptor to supervisor */
int fb_sv_conn = -1;

/** interceptor init has been run */
int ic_init_done = false;

/**
 * Stored PID
 * When getpid() returns a different value, we missed a fork() :-)
 */
int ic_pid;

/** Per thread variable which we turn on inside call interception */
__thread const char *intercept_on = NULL;

/** debugging flags */
int32_t debug_flags = 0;

/** Insert marker open()-s for strace, ltrace, etc. */
int insert_trace_markers = false;

/** Next ACK id*/
static int ack_id = 0;

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
  return __atomic_add_fetch(&ack_id, 1, __ATOMIC_SEQ_CST);
}

void fb_send_msg_and_check_ack(void* void_ic_msg, int fd) {
  int ack_num = get_next_ack_id();
  auto ic_msg = reinterpret_cast<msg::InterceptorMsg *>(void_ic_msg);
  ic_msg->set_ack_num(ack_num);
  fb_send_msg(*ic_msg, fd);

  msg::SupervisorMsg sv_msg;
  auto len = fb_recv_msg(&sv_msg, fd);
  assert(len > 0);
  assert(sv_msg.ack_num() == ack_num);
}

/** Compare compare pointers to char* like strcmp() for char* */
static int cmpstringpp(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
     pointers to char", but strcmp(3) arguments are "pointers
     to char", hence the following cast plus dereference */
  return strcmp(* (char * const *) p1, * (char * const *) p2);
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
  assert(strlen(fb_conn_string) + 1 < sizeof(remote.sun_path));

  for (int sock_id = 0; conn_ret == -1; sock_id++) {
    snprintf(remote.sun_path, sizeof(remote.sun_path), "%s%d", fb_conn_string, sock_id);
    conn_ret = ic_orig_connect(conn, (struct sockaddr *)&remote, sizeof(remote));
    if (conn_ret == -1 && errno != EADDRINUSE && errno != EISCONN) {
      break;
    }
  }

  if (conn_ret == -1) {
    perror("connect");
    assert(0 && "connection to supervisor failed");
  }

  if (fd > -1 && conn != fd) {
    int ret = ic_orig_dup3(conn, fd, O_CLOEXEC);
    if (ret == -1) {
      perror("dup3:");
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
  if (NULL != getenv("FB_INSERT_TRACE_MARKERS")) {
    insert_trace_markers = true;
  }

  init_interceptors();

  assert(intercept_on == NULL);
  intercept_on = "init";
  insert_debug_msg("initialization-begin");

  // init global variables

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  fb_init_supervisor_conn();

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

  for (char** cursor = argv; *cursor != NULL; cursor++) {
    proc->add_arg(*cursor);
  }

  /* make a sorted copy onf env */
  int env_len = 0;
  for (char** cursor = env; *cursor != NULL; cursor++) {
    env_len += 1;
  }

  char** env_copy = reinterpret_cast<char**>(malloc(env_len * sizeof(env[0])));
  memcpy(env_copy, env, env_len * sizeof(env[0]));

  qsort(env_copy, env_len, sizeof(env_copy[0]), cmpstringpp);

  /* send sorted env, omitting FB_SOCKET */
  for (int i = 0; i < env_len; i++) {
    const char *fb_socket = "FB_SOCKET=";
    if (strncmp(env_copy[i], fb_socket, strlen(fb_socket)) != 0) {
      proc->add_env_var(env_copy[i]);
    }
  }

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
    auto orig_underscore_exit = (void(*)(int)) dlsym(RTLD_NEXT, "_exit");
    (*orig_underscore_exit)(resp->exit_status());
  } else {
    if (resp->has_debug_flags()) {
      debug_flags = resp->debug_flags();
    }
  }
  ic_init_done = true;
  insert_debug_msg("initialization-end");
  intercept_on = NULL;

  free(env_copy);
}

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

  /* Destruction of global objects is not done here, because other exit handlers
   * may perform actions that need to be reported to the supervisor.
   * TODO(rbalint) add Valgrind suppress file
   */
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
  fb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
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
extern void fb_error(const char* msg) {
  msg::InterceptorMsg ic_msg;
  auto err = ic_msg.mutable_fb_error();
  err->set_msg(msg);
  fb_send_msg(ic_msg, fb_sv_conn);
}

/** Send debug message to supervisor if debug level is at least lvl */
void fb_debug(const char* msg) {
  msg::InterceptorMsg ic_msg;
  auto dbg = ic_msg.mutable_fb_debug();
  dbg->set_msg(msg);
  fb_send_msg(ic_msg, fb_sv_conn);
}


/** Add shared library's name to the file list */
int shared_libs_cb(struct dl_phdr_info *info, const size_t size, void *data) {
  auto *fl = (firebuild::msg::FileList*)data;
  // unused
  (void)size;

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
  fl->add_file(info->dlpi_name);

  return 0;
}

/* make auditing functions visible */
#pragma GCC visibility push(default)

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

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_init():
 * Add an entry, with a new empty protobuf, to our pool.
 */
void psfa_init(const posix_spawn_file_actions_t *p) {
  psfa_destroy(p);

  /* grow buffer if necessary */
  if (psfas_alloc == 0) {
    psfas_alloc = 4 /* whatever */;
    psfas = reinterpret_cast<psfa *>(malloc(sizeof(psfa) * psfas_alloc));
  } else if (psfas_num == psfas_alloc) {
    psfas_alloc *= 2;
    psfas = reinterpret_cast<psfa *>(realloc(psfas, sizeof(psfa) * psfas_alloc));
  }

  psfas[psfas_num].p = p;
  psfas[psfas_num].protobuf = new msg::PosixSpawnFileActions();
  psfas_num++;
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_destroy():
 * Remove the entry, freeing up the protobuf, from our pool.
 * Do not shrink psfas.
 */
void psfa_destroy(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      delete psfas[i].protobuf;
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
 * Append a corresponding record to our protobuf.
 */
void psfa_addopen(const posix_spawn_file_actions_t *p,
                  int fd,
                  const char *path,
                  int flags,
                  mode_t mode) {
  // FIXME remove cast
  msg::PosixSpawnFileActions *actions_msg = (msg::PosixSpawnFileActions *) psfa_find(p);
  assert(actions_msg);

  msg::PosixSpawnFileAction *action_msg = actions_msg->add_action();
  msg::PosixSpawnFileActionOpen *open_msg = action_msg->mutable_open();
  open_msg->set_fd(fd);
  open_msg->set_path(path);
  open_msg->set_flags(flags);
  open_msg->set_mode(mode);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_addclose():
 * Append a corresponding record to our protobuf.
 */
void psfa_addclose(const posix_spawn_file_actions_t *p,
                   int fd) {
  // FIXME remove cast
  msg::PosixSpawnFileActions *actions_msg = (msg::PosixSpawnFileActions *) psfa_find(p);
  assert(actions_msg);

  msg::PosixSpawnFileAction *action_msg = actions_msg->add_action();
  msg::PosixSpawnFileActionClose *close_msg = action_msg->mutable_close();
  close_msg->set_fd(fd);
}

/**
 * Additional bookkeeping to do after a successful posix_spawn_file_actions_adddup2():
 * Append a corresponding record to our protobuf.
 */
void psfa_adddup2(const posix_spawn_file_actions_t *p,
                  int oldfd,
                  int newfd) {
  // FIXME remove cast
  msg::PosixSpawnFileActions *actions_msg = (msg::PosixSpawnFileActions *) psfa_find(p);
  assert(actions_msg);

  msg::PosixSpawnFileAction *action_msg = actions_msg->add_action();
  msg::PosixSpawnFileActionDup2 *dup2_msg = action_msg->mutable_dup2();
  dup2_msg->set_oldfd(oldfd);
  dup2_msg->set_newfd(newfd);
}

/**
 * Find the additional protobuf for a given posix_spawn_file_actions.
 */
// FIXME msg::PosixSpawnFileActions *
void *psfa_find(const posix_spawn_file_actions_t *p) {
  for (int i = 0; i < psfas_num; i++) {
    if (psfas[i].p == p) {
      return psfas[i].protobuf;
    }
  }
  return NULL;
}

}  // extern "C"

}  // namespace firebuild

#pragma GCC visibility pop

