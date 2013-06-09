
#include <cassert>
#include <cstdarg>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#include "intercept.h"
#include "env.h"
#include "fb-messages.pb.h"
using namespace std;

#ifdef  __cplusplus
extern "C" {
#endif

static void fb_ic_load() __attribute__ ((constructor));

__pid_t (*ic_orig_getpid) (void);
__pid_t (*ic_orig_getppid) (void);
char * (*ic_orig_getcwd) (char *, size_t);

#ifdef  __cplusplus
}
#endif

/* global vars */
ic_fn_info ic_fn[IC_FN_IDX_MAX];

/**
 * Reset globally maintained information about intercepted funtions
 */
static void
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
}

/** buffer for getcwd */
#define CWD_BUFSIZE 4096
static char cwd_buf[CWD_BUFSIZE];

/**
 * Collect information about process the earliest possible, right
 * when interceptor library loads
 */
static void fb_ic_load()
{
  char **argv, **env, **cursor, *cwd_ret;
  __pid_t pid, ppid;
  ShortCutProcessQuery proc;

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  set_orig_fns();
  reset_fn_infos();
  get_argv_env(&argv, &env);
  pid = ic_orig_getpid();
  ppid = ic_orig_getppid();
  cwd_ret = ic_orig_getcwd(cwd_buf, CWD_BUFSIZE);
  assert(cwd_ret != NULL);

  proc.set_pid(pid);
  proc.set_ppid(ppid);
  proc.set_cwd(cwd_buf);

  for (cursor = argv; *cursor != NULL; cursor++) {
    proc.add_arg(*cursor);
  }

  for (cursor = env; *cursor != NULL; cursor++) {
    proc.add_env_var(*cursor);
  }

  // TODO query supervisor if we can shortcut this process
  //exit(ret);

}

