/*
 * Interceptor library definitions
 */

#ifndef _INTERCEPT_H
#define _INTERCEPT_H

#include <dlfcn.h>

/* create global array indexed by intercepted function's id */
#define IC_VOID(_ret_type, name, _parameters, _body)	\
  IC_FN_IDX_##name,
#define IC(_ret_type, name, _parameters, _body)	\
  IC_FN_IDX_##name,
#define IC_GENERIC(_ret_type, name, _parameters, _body)	\
  IC_FN_IDX_##name,
#define IC_GENERIC_VOID(_ret_type, name, _parameters, _body)	\
  IC_FN_IDX_##name,

/* we need to include every file using IC() macro to create index for all
 * functions */
enum {
#include "ic_file_ops.h"
  IC_FN_IDX_MAX
};

#undef IC_VOID
#undef IC
#undef IC_GENERIC
#undef IC_GENERIC_VOID

typedef struct {
  bool called;
} ic_fn_info;

extern ic_fn_info ic_fn[IC_FN_IDX_MAX];

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

/**
 * Just send the intercepted function's name
 */
#define IC_GENERIC(ret_type, name, parameters, body)		\
  IC(ret_type, name, parameters,				\
     {								\
       if (!ic_fn[IC_FN_IDX_##name].called) {			\
	 GenericCall m;						\
	 m.set_call(#name);					\
	 /* TODO send to supervisor */				\
	 cerr << "intercept generic call: " <<#name << endl;	\
	 ic_fn[IC_FN_IDX_##name].called = true;			\
       }							\
       body;							\
     })

#define IC_GENERIC_VOID(ret_type, name, parameters, body)		\
  IC_VOID(ret_type, name, parameters,					\
	  {								\
	    if (!ic_fn[IC_FN_IDX_##name].called) {			\
	      GenericCall m;						\
	      m.set_call(#name);					\
	      /* TODO send to supervisor */				\
	      cerr << "intercept generic call: " <<#name << endl;	\
	      ic_fn[IC_FN_IDX_##name].called = true;			\
	    }								\
	    body;							\
	  })
