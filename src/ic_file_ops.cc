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

/* */
static void
intercept_open (const char *file, const int flags, const int mode,
	       const int ret, const int error_no)
{
  OpenFile m;
  m.set_pid(getpid());
  m.set_file(file);
  m.set_flags(flags);
  m.set_mode(mode);
  m.set_error_no(error_no);

  cout << "intercept open!" << endl;
  // TODO send to supervisor and collect file status if needed
}

/**
 * Intercept open variants with varible length arg list.
 * mode is filled based on presence of O_CREAT flag
 */
#define IC_OPEN_VA(ret_type, name, parameters, body)			\
  IC(ret_type, name, parameters,					\
     {									\
       int open_errno;							\
       mode_t mode = 0;							\
       if (__oflag & O_CREAT) {						\
	 va_list ap;							\
	 va_start(ap, __oflag);						\
	 mode = va_arg(ap, mode_t);					\
	 va_end(ap);							\
       }								\
									\
       body;								\
       open_errno = errno;						\
       intercept_open(__file, __oflag, mode, ret, open_errno);		\
       errno = open_errno;						\
     })


IC_OPEN_VA(int, open, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, open64, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, openat, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

IC_OPEN_VA(int, openat64, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

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
