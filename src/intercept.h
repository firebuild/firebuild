/*
 * Interceptor library definitions
 */

#ifndef _INTERCEPT_H
#define _INTERCEPT_H

#include <dlfcn.h>


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

#endif
