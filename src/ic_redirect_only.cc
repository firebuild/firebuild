/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

/* Exported functions calling other functions directly without dlsym lookup
 * tricks */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cstdarg>
#include <cstdlib>

#ifdef  __cplusplus
extern "C" {
#endif


/**
 * generator for intercepting various execl.. calls
 */
#define IC_EXECLXX(with_p, with_e)                                      \
  extern int execl##with_p##with_e(const char *path,  const char *arg, ...) \
  {                                                                     \
    va_list ap;                                                         \
    /* tricky consts, TRUE means we get and pass env */                 \
    const bool call_ = false, call_e = true;                            \
    unsigned int argc = 0, argc_size = 16;                              \
    char **argv = static_cast<char **>(malloc(argc_size * sizeof(char*))); \
    va_start(ap, arg);                                                  \
    /* silence ... unused warnings */                                   \
    (void)call_; (void)call_e;                                          \
    argv[argc] = const_cast<char *>(arg);                               \
    while (argv[argc]) {                                                \
      argv[++argc] = static_cast<char *>(va_arg(ap, char*));            \
      if (argc == argc_size - 1) {                                      \
        argc_size *= 2;                                                 \
        argv = static_cast<char **>(realloc(argv, argc_size * sizeof(char*))); \
      }                                                                 \
    }                                                                   \
    char **envp;                                                        \
    if (call_##with_e) {                                                \
      envp = static_cast<char **>(va_arg(ap, char**));                  \
    }                                                                   \
    va_end(ap);                                                         \
                                                                        \
    int ret;                                                            \
    if (call_##with_e == true) {                                        \
      ret = execv##with_p##e(path, argv, envp);                         \
    } else {                                                            \
      ret = execv##with_p(path, argv);                                  \
    }                                                                   \
    free(argv);                                                         \
    return ret;                                                         \
  }                                                                     \

/* make redirected functions visible */
#pragma GCC visibility push(default)


IC_EXECLXX(, )
IC_EXECLXX(, e)
IC_EXECLXX(p, )
IC_EXECLXX(p, e)

/**
 * vfork simply calling fork
 *
 * vfork interception would be a bit complicated to implement properly
 * and most of the programs will work properly with fork
 */
extern pid_t vfork(void) {
  return fork();
}

/**
 * creat calling equivalent open
 */
extern int creat(const char *pathname, mode_t mode) {
  return open(pathname, (O_CREAT|O_WRONLY|O_TRUNC), mode);
}

/**
 * creat64 calling equivalent open64
 */
extern int creat64(const char *pathname, mode_t mode) {
  return open64(pathname, (O_CREAT|O_WRONLY|O_TRUNC), mode);
}

#pragma GCC visibility pop

#ifdef  __cplusplus
}
#endif
