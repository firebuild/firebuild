/* from fcntl.h */

#include <fcntl.h>
#include <cstdarg>
#include <cassert>
#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <sys/time.h>
#include <sys/resource.h>

#include "intercept.h"
#include "fb-messages.pb.h"

using namespace std;

// TODO? 
//int fcntl (int __fd, int __cmd, ...);

/* Intercept open variants */
static void
intercept_open (const char *file, const int flags, const int mode,
	       const int ret, const int error_no)
{
  InterceptorMsg ic_msg;
  OpenFile *m;
  m = ic_msg.mutable_open_file();
  m->set_file(file);
  m->set_flags(flags);
  m->set_mode(mode);
  m->set_ret(ret);
  m->set_error_no(error_no);

  fb_send_msg(ic_msg, fb_sv_conn);
}


/* Intercept creat variants */
static void
intercept_create (const char *file, const int mode,
	       const int ret, const int error_no)
{
  InterceptorMsg ic_msg;
  CreateFile *m;
  m = ic_msg.mutable_create_file();
  m->set_file(file);
  m->set_mode(mode);
  m->set_ret(ret);
  m->set_error_no(error_no);

  fb_send_msg(ic_msg, fb_sv_conn);
}

#define IC2_SIMPLE_INT_1P(ics_pmtype, ics_pmname, ics_pmattrname, ics_ptype) \
  static void								\
  intercept_##ics_pmname (ics_ptype ics_p, int ret)			\
  {									\
    InterceptorMsg ic_msg;						\
    ics_pmtype *m;							\
    int saved_errno = errno;						\
									\
    m = ic_msg.mutable_##ics_pmname();					\
    m->set_##ics_pmattrname(ics_p);							\
    if (ret == -1) {							\
      m->set_error_no(saved_errno);					\
    }									\
									\
    fb_send_msg(ic_msg, fb_sv_conn);					\
    errno = saved_errno;						\
  }

/* Intercept unlink */
IC2_SIMPLE_INT_1P(UnLink, unlink, path, const char *)
/* Intercept chdir */
IC2_SIMPLE_INT_1P(ChDir, chdir, dir, const char *)
/* Intercept fchdir */
IC2_SIMPLE_INT_1P(FChDir, fchdir, dir, const int)
/* Intercept close */
IC2_SIMPLE_INT_1P(Close, close, fd, const int)
/* Intercept rmdir */
IC2_SIMPLE_INT_1P(RmDir, rmdir, dir, const char *)

/* Intercept getcwd variants */
static void
intercept_getcwd (const char *dir)
{
  InterceptorMsg ic_msg;
  GetCwd *m;
  int saved_errno = errno;

  m = ic_msg.mutable_get_cwd();
  if (dir != NULL) {
    m->set_dir(dir);
  } else {
    m->set_error_no(saved_errno);
  }

  fb_send_msg(ic_msg, fb_sv_conn);
  errno = saved_errno;
}


static void
intercept_exit (const int status)
{
  InterceptorMsg ic_msg;
  SupervisorMsg sv_msg;
  Exit *m;
  struct rusage ru;

  m = ic_msg.mutable_exit();
  m->set_exit_status(status);
  getrusage(RUSAGE_SELF, &ru);
  m->set_utime_m(ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000);
  m->set_stime_m(ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000);
  fb_send_msg(ic_msg, fb_sv_conn);
  fb_recv_msg(sv_msg, fb_sv_conn);
  if (!sv_msg.ack()) {
    // something unexpected happened ...
    assert(0);
  }

}

/* make intercepted functions visible */
static __pid_t
intercept_fork (const __pid_t ret)
{
  InterceptorMsg ic_msg;
  __pid_t pid;

  if (ret == 0) {
    // child
    ForkChild *m;
    reset_fn_infos();
    ic_pid = pid = ic_orig_getpid();
    // unlock global interceptor lock if it is locked
    pthread_mutex_trylock(&ic_global_lock);
    pthread_mutex_unlock(&ic_global_lock);
    // reconnect to supervisor
    ic_orig_close(fb_sv_conn);
    fb_sv_conn = -1;
    init_supervisor_conn();
    m = ic_msg.mutable_fork_child();
    m->set_pid(pid);
    m->set_ppid(ic_orig_getppid());
    fb_send_msg(ic_msg, fb_sv_conn);
  } else {
    // parent
    ForkParent *m;
    m = ic_msg.mutable_fork_parent();
    m->set_pid(ic_pid);
    m->set_child_pid(ret);
    fb_send_msg(ic_msg, fb_sv_conn);
  }
  return ret;
}

/* make intercepted functions visible */
#pragma GCC visibility push(default)

#include "ic_file_ops.h"

#pragma GCC visibility pop
