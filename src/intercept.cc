
#include <cassert>
#include <cstdarg>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/socket.h>
#include <sys/un.h>


#include "intercept.h"
#include "env.h"
#include "fb-messages.pb.h"
#include "firebuild_common.h"

using namespace std;

#ifdef  __cplusplus
extern "C" {
#endif

static void fb_ic_cleanup() __attribute__ ((destructor));

#ifdef  __cplusplus
}
#endif

/* global vars */
ic_fn_info ic_fn[IC_FN_IDX_MAX];

/** file fd states */
std::vector<fd_state> fd_states;

/** Global lock for manipulating fd states */
pthread_mutex_t ic_fd_states_lock;

/* original intercepted functions */
__pid_t (*ic_orig_getpid) (void);
__pid_t (*ic_orig_getppid) (void);
char * (*ic_orig_getcwd) (char *, size_t);
size_t (*ic_orig_confstr) (int, char *, size_t);
ssize_t(*ic_orig_write)(int, const void *, size_t);
ssize_t(*ic_orig_read)(int, const void *, size_t);
ssize_t (*ic_orig_readlink) (const char*, char*, size_t);
int (*ic_orig_close) (int);

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

/**
 * Reset globally maintained information about intercepted funtions
 */
void
reset_fn_infos ()
{
  int i;
  for (i = 0; i < IC_FN_IDX_MAX ; i++) {
    ic_fn[i].called = false;
  }
}

/**
 * Get pointer to a function implemented in the next shared
 * library. In our case this is a function we intercept.
 * @param[in] name function's name
 */
static void *
get_orig_fn (const char* name)
{
  void * function = dlsym(RTLD_NEXT, name);
  assert(function);
  return function;
}

/**
 * Get pointers to all the functions we intercept but we also want to use
 */
static void
set_orig_fns ()
{
  ic_orig_getpid = (__pid_t(*)(void))get_orig_fn("getpid");
  ic_orig_getppid = (__pid_t(*)(void))get_orig_fn("getppid");
  ic_orig_getcwd = (char *(*)(char *, size_t))get_orig_fn("getppid");
  ic_orig_confstr = (size_t (*)(int, char *, size_t))get_orig_fn("confstr");
  ic_orig_write = (ssize_t(*)(int, const void *, size_t))get_orig_fn("write");
  ic_orig_read = (ssize_t(*)(int, const void *, size_t))get_orig_fn("read");
  ic_orig_readlink = (ssize_t (*) (const char*, char*, size_t))get_orig_fn("readlink");
  ic_orig_close = (int (*) (int))get_orig_fn("close");


}

/**  Set up supervisor connection */
void
init_supervisor_conn () {

  struct sockaddr_un remote;
  size_t len;

  if (fb_conn_string == NULL) {
    fb_conn_string = strdup(getenv("FB_SOCKET"));
  }

  if ((fb_sv_conn = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1) {
    assert(fb_sv_conn > STDERR_FILENO);
    assert(fb_sv_conn != -1);
  }

  remote.sun_family = AF_UNIX;
  assert(strlen(fb_conn_string) < sizeof(remote.sun_path));
  strncpy(remote.sun_path, fb_conn_string, sizeof(remote.sun_path));

  len = strlen(remote.sun_path) + sizeof(remote.sun_family);
  if (connect(fb_sv_conn, (struct sockaddr *)&remote, len) == -1) {
    assert(0 && "connection to supervisor failed");
  }
}

/**
 * Initialize interceptor's data structures and sync with supervisor
 */
static void fb_ic_init()
{
  char **argv, **env, **cursor, *cwd_ret;
  char cwd_buf[CWD_BUFSIZE];
  __pid_t pid, ppid;
  ShortCutProcessQuery *proc;
  ShortCutProcessResp * resp;
  InterceptorMsg ic_msg;
  SupervisorMsg sv_msg;

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  set_orig_fns();
  reset_fn_infos();

  init_supervisor_conn();

  get_argv_env(&argv, &env);
  ic_pid = pid = ic_orig_getpid();
  ppid = ic_orig_getppid();
  cwd_ret = ic_orig_getcwd(cwd_buf, CWD_BUFSIZE);
  assert(cwd_ret != NULL);

  proc = ic_msg.mutable_scproc_query();

  proc->set_pid(pid);
  proc->set_ppid(ppid);
  proc->set_cwd(cwd_buf);

  for (cursor = argv; *cursor != NULL; cursor++) {
    proc->add_arg(*cursor);
  }

  for (cursor = env; *cursor != NULL; cursor++) {
    proc->add_env_var(*cursor);
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

  fb_send_msg(ic_msg, fb_sv_conn);
  fb_recv_msg(sv_msg, fb_sv_conn);

  resp = sv_msg.mutable_scproc_resp();
  // we may return immediately if supervisor decides that way
  if (resp->shortcut()) {
    if (resp->has_exit_status()) {
      exit(resp->exit_status());
    } else {
      // TODO send error
    }
  }
  ic_init_done = true;
}

/**
 * Collect information about process the earliest possible, right
 * when interceptor library loads or when the first interceped call happens
 */
void fb_ic_load()
{
  if (!ic_init_done) {
    fb_ic_init();
  }
}


static void fb_ic_cleanup()
{
  // Optional:  Delete all global objects allocated by libprotobuf.
  google::protobuf::ShutdownProtobufLibrary();
  ic_orig_close(fb_sv_conn);
}


/** wrapper for write() retrying on recoverable errors*/
ssize_t fb_write_buf(int fd, const void *buf, const size_t count)
{
  pthread_mutex_lock(&ic_global_lock);
  FB_IO_OP_BUF(ic_orig_write, fd, buf, count, {pthread_mutex_unlock(&ic_global_lock);});
}

/** wrapper for write() retrying on recoverable errors*/
ssize_t fb_read_buf(int fd, const void *buf, const size_t count)
{
  pthread_mutex_lock(&ic_global_lock);
  FB_IO_OP_BUF(ic_orig_read, fd, buf, count, {pthread_mutex_unlock(&ic_global_lock);});
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
unsigned int
la_version(unsigned int version)
{
  return version;
}

/**
 * Send path to supervisor whenever the dynamic linker wants to load a shared
 * library
 */
char *
la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag)
{
  InterceptorMsg ic_msg;
  LAObjSearch *los = ic_msg.mutable_la_objsearch();

  // unused
  (void)cookie;

  fb_ic_load();

  los->set_name(name);
  los->set_flag(flag);
  fb_send_msg(ic_msg, fb_sv_conn);

  return const_cast<char*>(name);
}

/**
 * Send path to supervisor whenever the dynamic linker loads a shared library
 */
unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
  InterceptorMsg ic_msg;
  LAObjOpen *los = ic_msg.mutable_la_objopen();

  // unused
  (void)lmid;
  (void)cookie;

  fb_ic_load();

  los->set_name(map->l_name);
  fb_send_msg(ic_msg, fb_sv_conn);

  return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}


#ifdef  __cplusplus
}
#endif

#pragma GCC visibility pop
