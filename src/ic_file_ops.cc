/* from fcntl.h */

#include <fcntl.h>
#include <cstdarg>
#include <cassert>
#include <errno.h>
#include <unistd.h>
#include <iostream>

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
  OpenFile m;
  m.set_pid(ic_orig_getpid());
  m.set_file(file);
  m.set_flags(flags);
  m.set_mode(mode);
  m.set_ret(ret);
  m.set_error_no(error_no);

  cout << "intercept open!" << endl;
  // TODO send to supervisor and collect file status if needed
}


/* Intercept creat variants */
static void
intercept_create (const char *file, const int mode,
	       const int ret, const int error_no)
{
  CreateFile m;
  m.set_pid(ic_orig_getpid());
  m.set_file(file);
  m.set_mode(mode);
  m.set_ret(ret);
  m.set_error_no(error_no);

  cout << "intercept create!" << endl;
  // TODO send to supervisor
}


/* Intercept close */
static void
intercept_close (const int fd, const int ret)
{
  CloseFile m;
  m.set_pid(ic_orig_getpid());
  m.set_fd(fd);
  m.set_ret(ret);

  cout << "intercept close!" << endl;
  // TODO send to supervisor
}


/* TODO finish */
static void
intercept_exit (const int /* status*/)
{
  GenericCall m;
  m.set_call("exit");

  cout << "intercept exit!" << endl;
  // TODO send to supervisor
}

/* make intercepted functions visible */
#pragma GCC visibility push(default)

#include "ic_file_ops.h"

#pragma GCC visibility pop
