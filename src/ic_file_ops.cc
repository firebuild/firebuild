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


/* Intercept close */
static void
intercept_close (const int fd, const int ret)
{
  InterceptorMsg ic_msg;
  CloseFile *m;
  m = ic_msg.mutable_close_file();
  m->set_fd(fd);
  m->set_ret(ret);

  fb_send_msg(ic_msg, fb_sv_conn);
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
#pragma GCC visibility push(default)

#include "ic_file_ops.h"

#pragma GCC visibility pop
