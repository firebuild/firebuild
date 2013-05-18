
#include <fcntl.h>
#include <dlfcn.h>
#include <iostream>
#include <cassert>
#include <cstdarg>
#include <unistd.h>

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


/**
 * Intercept call returning void
 */
#define IC_VOID(ret_type, name, parameters, body)			\
  extern ret_type (name) parameters					\
  {									\
    /* original intercepted function */					\
    static ret_type (*orig_fn)parameters;				\
    if (!orig_fn) {							\
      orig_fn = (ret_type(*)parameters)dlsym(RTLD_NEXT, #name);		\
      assert(orig_fn);							\
  }									\
  { 									\
    body; /* this is where interceptor function body goes */		\
  }									\
}

/**
 * Intercept call 
 */
#define IC(ret_type, name, parameters, body)				\
  IC_VOID(ret_type, name, parameters,					\
	  { ret_type ret;						\
	    body;							\
	    return ret;							\
	  })

/* from fcntl.h */

// TODO? 
//int fcntl (int __fd, int __cmd, ...);

/**
 * Intercept open variants with varible length arg list.
 * mode is filled based on presence of O_CREAT flag
 */
#define IC_OPEN_VA(ret_type, name, parameters, body)			\
  IC(ret_type, name, parameters,					\
     {									\
       mode_t mode = 0;							\
       if (__oflag & O_CREAT) {						\
	 va_list ap;							\
	 va_start(ap, __oflag);						\
	 mode = va_arg(ap, mode_t);					\
	 va_end(ap);							\
       }								\
       									\
       body;								\
     })


IC_OPEN_VA(int, open, (__const char *__file, int __oflag, ...),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__file, __oflag, mode);
	   })

IC_OPEN_VA(int, open64, (__const char *__file, int __oflag, ...),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__file, __oflag, mode);
	   })

IC_OPEN_VA(int, openat, (int __fd, int __oflag, ...),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__fd, __oflag, mode);
	   })

IC_OPEN_VA(int, openat64, (int __fd, int __oflag, ...),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__fd, __oflag, mode);
	   })

IC(int, creat, (__const char *__file, __mode_t __mode),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__file, __mode);
	   })

IC(int, creat64, (__const char *__file, __mode_t __mode),
	   {
	     cout << "intercept!" << endl;
	     ret = orig_fn(__file, __mode);
	   })
// TODO?
// lockf lockf64
