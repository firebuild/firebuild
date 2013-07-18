/* Exported functions calling other functions directly without dlsym lookup tricks */

#include <unistd.h>
#include <cstdarg>
#include <cstdlib>

#ifdef  __cplusplus
extern "C" {
#endif


/**
 * generator for intercepting various execl.. calls
 */
#define IC_EXECLXX(with_p, with_e)					\
  extern int execl##with_p##with_e(__const char *__path,  __const char *__arg, ...) \
  {									\
    va_list ap;								\
    char **envp;							\
    int ret;								\
    const bool call_ = false, call_e = true; /* tricky consts, TRUE means we get and pass env */ \
    unsigned int argc = 0, argc_size = 16;				\
    char **argv = static_cast<char **>(malloc(argc_size * sizeof(char*))); \
    va_start(ap, __arg);						\
    /* silence ... unused warnings */					\
    (void)call_; (void)call_e;						\
    argv[argc] = const_cast<char *>(__arg);				\
    while (argv[argc]) {						\
      argv[++argc] = static_cast<char *>(va_arg(ap, char*));		\
      if (argc == argc_size - 1) {					\
	argc_size *= 2;							\
	argv = static_cast<char **>(realloc(argv, argc_size * sizeof(char*))); \
      }									\
    }									\
    if (call_##with_e) {						\
      envp = static_cast<char **>(va_arg(ap, char**));			\
    }									\
    va_end(ap);								\
									\
    if (call_##with_e == true) {					\
      ret = execv##with_p##e(__path, argv, envp);			\
    } else {								\
      ret = execv##with_p(__path, argv);				\
    }									\
    free (argv);							\
    return ret;								\
  }									\

/* make redirected functions visible */
#pragma GCC visibility push(default)


IC_EXECLXX ( , )
IC_EXECLXX ( , e)
IC_EXECLXX (p, )
IC_EXECLXX (p, e)

/**
 * vfork simply calling fork
 *
 * vfork interception would be a bit complicated to implement properly
 * and most of the programs will work properly with fork
 */
extern __pid_t vfork (void)
{
  return fork();
}

#pragma GCC visibility pop

#ifdef  __cplusplus
}
#endif
