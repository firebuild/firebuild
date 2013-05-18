
//#define _GNU_SOURCE
#include <fcntl.h>
#include <dlfcn.h>
#include <iostream>
#include <cassert>
#include <cstdarg>

using namespace std;

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
