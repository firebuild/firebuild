/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

/*
 * Interceptor library definitions
 */

#ifndef FIREBUILD_INTERCEPT_H_
#define FIREBUILD_INTERCEPT_H_

#include <dlfcn.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/socket.h>

#include <string>
#include <vector>

#include "firebuild_common.h"

/**
 * Intercept call
 */
#define IC(ret_type, name, parameters, body)    \
  IC_VOID(ret_type, name, parameters,           \
          { ret_type ret;                       \
            body;                               \
            insert_end_marker(__func__);        \
            intercept_on = false;               \
            return ret;                         \
          })

/**
 * Just send the intercepted function's name
 */
#define IC_GENERIC(ret_type, name, parameters, body)    \
  IC(ret_type, name, parameters,                        \
     {                                                  \
       if (!ic_fn[IC_FN_IDX_##name].called) {           \
         msg::InterceptorMsg ic_msg;                    \
         auto m = ic_msg.mutable_gen_call();            \
         m->set_call(#name);                            \
         fb_send_msg(ic_msg, fb_sv_conn);               \
         ic_fn[IC_FN_IDX_##name].called = true;         \
       }                                                \
       body;                                            \
     })

#define IC_GENERIC_VOID(ret_type, name, parameters, body)   \
  IC_VOID(ret_type, name, parameters,                       \
          {                                                 \
            if (!ic_fn[IC_FN_IDX_##name].called) {          \
              msg::InterceptorMsg ic_msg;                   \
              auto m = ic_msg.mutable_gen_call();           \
              m->set_call(#name);                           \
              fb_send_msg(ic_msg, fb_sv_conn);              \
              ic_fn[IC_FN_IDX_##name].called = true;        \
            }                                               \
            body;                                           \
          })


/* create global array indexed by intercepted function's id */
#define IC_VOID(_ret_type, name, _parameters, _body)    \
  IC_FN_IDX_##name,

/* we need to include every file using IC() macro to create index for all
 * functions */
enum {
#include "ic_file_ops.h"
  IC_FN_IDX_MAX
};
#undef IC_VOID

namespace firebuild {
/* create ic_orig_... version of intercepted function */
#define IC_VOID(ret_type, name, parameters, _body)      \
  extern ret_type(*ic_orig_##name)parameters;

/* we need to include every file using IC() macro to create ic_orig_... version
 * for all functions */
#include "ic_file_ops.h"
#undef IC_VOID

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
extern std::vector<fd_state> *fd_states;

/** Global lock for manipulating fd states */
extern pthread_mutex_t ic_fd_states_lock;

/** buffer size for getcwd */
#define CWD_BUFSIZE 4096

/** Reset globally maintained information about intercepted funtions */
extern void reset_fn_infos();

/**  Set up supervisor connection */
extern void init_supervisor_conn();

/** Global lock for serializing critical interceptor actions */
extern pthread_mutex_t ic_global_lock;

/** Get next unique ACK id */
extern int get_next_ack_id();

/** Connection file descriptor to supervisor */
extern int fb_sv_conn;

/** interceptor init has been run */
extern bool ic_init_done;

/** interceptor handled exit */
extern bool fb_exit_handled;

/** the appliation called exec[vpe] but we have not exited from unsuccesful
 * exec() yet
 */
extern bool fb_exec_called;

/** Insert begin marker strace, ltrace, etc. */
extern void insert_begin_marker(const std::string&);

/** Insert end marker strace, ltrace, etc. */
extern void insert_end_marker(const std::string&);

/**
 * Stored PID
 * When getpid() returns a different value, we missed a fork() :-)
 */
extern int ic_pid;

/** Per thread variable which we turn on inside call interception */
extern __thread bool intercept_on;

}  // namespace firebuild

#ifdef  __cplusplus
extern "C" {
#endif

/** Add shared library's name to the file list */
extern int shared_libs_cb(struct dl_phdr_info *info, size_t size, void *data);

extern void fb_ic_load() __attribute__((constructor));
extern void handle_exit(const int status, void*);
extern int __libc_start_main(int (*main)(int, char **, char **),
                             int argc, char **ubp_av,
                             void (*init)(void), void (*fini)(void),
                             void (*rtld_fini)(void), void (* stack_end));

#ifdef  __cplusplus
}
#endif

/**
 * Intercept call returning void
 */
#define IC_VOID(ret_type, name, parameters, body)                       \
  extern ret_type(name)parameters                                       \
  {                                                                     \
    /* local name for original intercepted function */                  \
    ret_type(* orig_fn)parameters = ic_orig_##name;                     \
    /* If we are called before the constructor we have to look up */    \
    /* function for ourself. This happens once per process run. */      \
    if (!orig_fn) {                                                     \
      orig_fn = (ret_type(*)parameters)dlsym(RTLD_NEXT, #name);         \
      assert(orig_fn);                                                  \
    }                                                                   \
    assert(intercept_on == false);                                      \
    intercept_on = true;                                                \
    insert_begin_marker(__func__);                                      \
    fb_ic_load();                                                       \
    {                                                                   \
      body; /* this is where interceptor function body goes */          \
    }                                                                   \
    insert_end_marker(__func__);                                        \
    intercept_on = false;                                               \
  }

#endif  // FIREBUILD_INTERCEPT_H_
