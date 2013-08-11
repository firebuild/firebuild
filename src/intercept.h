/*
 * Interceptor library definitions
 */

#ifndef _INTERCEPT_H
#define _INTERCEPT_H

#include <dlfcn.h>
#include <pthread.h>
#include <vector>

#include "firebuild_common.h"
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

/** file usage state */
typedef struct {
  bool read; /** file has been read */
  bool written; /** file has been written to */
} fd_state;

/** file fd states */
extern std::vector<fd_state> fd_states;

/** Global lock for manipulating fd states */
extern pthread_mutex_t ic_fd_states_lock;

/** buffer size for getcwd */
#define CWD_BUFSIZE 4096

extern __pid_t (*ic_orig_getpid) (void);
extern __pid_t (*ic_orig_getppid) (void);
extern char * (*ic_orig_getcwd) (char *__buf, size_t __size);
extern size_t (*ic_orig_confstr) (int, char *, size_t);
extern ssize_t (*ic_orig_write) (int, const void*, size_t);
extern ssize_t (*ic_orig_read) (int, const void*, size_t);
extern ssize_t (*ic_orig_readlink) (const char*, char*, size_t);
extern int (*ic_orig_close) (int);
extern void* (*ic_orig_dlopen) (const char *, int);
extern int (*ic_orig_socket) (int, int, int);

/** Reset globally maintained information about intercepted funtions */
extern void reset_fn_infos ();

/**  Set up supervisor connection */
extern void init_supervisor_conn ();

/** Global lock for serializing critical interceptor actions */
extern pthread_mutex_t ic_global_lock;

/** Connection file descriptor to supervisor */
extern int fb_sv_conn;

/** interceptor init has been run */
extern bool ic_init_done;

/** interceptor handled exit */
extern bool fb_exit_handled;

/** Add shared library's name to the file list */
extern int shared_libs_cb(struct dl_phdr_info *info, size_t size, void *data);

/**
 * Stored PID
 * When getpid() returns a different value, we missed a fork() :-)
 */
extern int ic_pid;

/** Per thread variable which we turn on inside call interception */
extern __thread bool intercept_on;

#ifdef  __cplusplus
extern "C" {
#endif

extern void fb_ic_load() __attribute__ ((constructor));
extern void handle_exit (const int status, void*);

#ifdef  __cplusplus
}
#endif

/**
 * Intercept call returning void
 */
#define IC_VOID(ret_type, name, parameters, body)			\
  extern ret_type (name) parameters					\
  {									\
    /* original intercepted function */					\
    static ret_type (*orig_fn)parameters = NULL;			\
    if (!orig_fn) {							\
      orig_fn = (ret_type(*)parameters)dlsym(RTLD_NEXT, #name);		\
      assert(orig_fn);							\
  }									\
    assert(intercept_on == false);					\
    intercept_on = true;						\
    fb_ic_load();							\
  { 									\
    body; /* this is where interceptor function body goes */		\
  }									\
  intercept_on = false;							\
}

/**
 * Intercept call 
 */
#define IC(ret_type, name, parameters, body)				\
  IC_VOID(ret_type, name, parameters,					\
	  { ret_type ret;						\
	    body;							\
	    intercept_on = false;					\
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
	 InterceptorMsg ic_msg;					\
	 GenericCall *m;					\
	 m = ic_msg.mutable_gen_call();				\
	 m->set_call(#name);					\
	 fb_send_msg(ic_msg, fb_sv_conn);			\
	 ic_fn[IC_FN_IDX_##name].called = true;			\
       }							\
       body;							\
     })

#define IC_GENERIC_VOID(ret_type, name, parameters, body)		\
  IC_VOID(ret_type, name, parameters,					\
	  {								\
	    if (!ic_fn[IC_FN_IDX_##name].called) {			\
	      InterceptorMsg ic_msg;					\
	      GenericCall *m;						\
	      m = ic_msg.mutable_gen_call();				\
	      m->set_call(#name);					\
	      fb_send_msg(ic_msg, fb_sv_conn);				\
	      ic_fn[IC_FN_IDX_##name].called = true;			\
	    }								\
	    body;							\
	  })
