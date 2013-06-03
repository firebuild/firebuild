#ifndef _INTERCEPTED_ORIGINAL_FUNCTIONS_H
#define _INTERCEPTED_ORIGINAL_FUNCTIONS_H

#ifdef  __cplusplus
extern "C" {
#endif

extern __pid_t (*ic_orig_getpid) (void);
extern __pid_t (*ic_orig_getppid) (void);
extern char * (*ic_orig_getcwd) (char *__buf, size_t __size);

#ifdef  __cplusplus
}
#endif

#endif /* _INTERCEPTED_ORIGINAL_FUNCTIONS_H */

