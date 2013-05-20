
#include <cassert>
#include <cstdarg>
#include <unistd.h>
#include <errno.h>

#include "intercept.h"
#include "env.h"
#include "fb-messages.pb.h"

using namespace std;

#ifdef  __cplusplus
extern "C" {
#endif

static void fb_ic_load() __attribute__ ((constructor));

#ifdef  __cplusplus
}
#endif

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

  get_argv_env(&argv, &env);
  pid = getpid();
  ppid = getppid();
  cwd_ret = getcwd(cwd_buf, CWD_BUFSIZE);
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

