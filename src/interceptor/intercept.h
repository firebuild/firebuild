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
#include <sys/types.h>
#include <spawn.h>

#include <string>
#include <vector>

#include "common/firebuild_common.h"
#include "fb-messages.pb.h"

namespace firebuild {

/** file usage state */
typedef struct {
  bool read; /** file has been read */
  bool written; /** file has been written to */
} fd_state;

/** file fd states */
extern std::vector<fd_state> *fd_states;

/** Global lock for manipulating fd states */
extern pthread_mutex_t ic_fd_states_lock;

/** Global lock for preventing parallel system and popen calls */
extern pthread_mutex_t ic_system_popen_lock;

/** buffer size for getcwd */
#define CWD_BUFSIZE 4096

/** Reset globally maintained information about intercepted functions */
extern void reset_fn_infos();

/** Connect to supervisor */
extern int fb_connect_supervisor(int fd);
/** Set up main supervisor connection */
extern void fb_init_supervisor_conn();

/** Global lock for serializing critical interceptor actions */
extern pthread_mutex_t ic_global_lock;

/* Send message and wait for ACK */
extern void fb_send_msg_and_check_ack(firebuild::msg::InterceptorMsg& ic_msg, int fd);

/** Connection file descriptor to supervisor */
extern int fb_sv_conn;

/** interceptor init has been run */
extern bool ic_init_done;

/** Insert debug message */
extern void insert_debug_msg(const std::string&);

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
extern __thread const char *intercept_on;

}  // namespace firebuild

#ifdef  __cplusplus
extern "C" {
#endif

/** Add shared library's name to the file list */
extern int shared_libs_cb(struct dl_phdr_info *info, size_t size, void *data);

extern void fb_ic_load() __attribute__((constructor));
extern void on_exit_handler(const int status, void*);
extern void handle_exit(const int status);
extern int __libc_start_main(int (*main)(int, char **, char **),
                             int argc, char **ubp_av,
                             void (*init)(void), void (*fini)(void),
                             void (*rtld_fini)(void), void *stack_end);

#ifdef  __cplusplus
}  // extern "C"
#endif


#endif  // FIREBUILD_INTERCEPT_H_
