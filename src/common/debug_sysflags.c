/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "common/debug_sysflags.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Convenience macros to debug-print a bitfield. */

#define DEBUG_BITMAP_START(f, var) \
  { \
    const char *sep = "";

#define DEBUG_BITMAP_FLAG(f, var, flag) \
    if (var & flag) { \
      fprintf(f, "%s%s", sep, #flag); \
      var &= ~flag; \
      sep = "|"; \
    }

#define DEBUG_BITMAP_END_OCT(f, flags) \
    if (flags) { \
      /* Remaining unrecognized flags */ \
      fprintf(f, "%s0%o", sep, flags); \
    } \
  }

#define DEBUG_BITMAP_END_HEX(f, flags) \
    if (flags) { \
      /* Remaining unrecognized flags */ \
      fprintf(f, "%s0x%X", sep, flags); \
    } \
  }

/* Convenience macros to debug-print a variable that is supposed to have one of several values. */

#define DEBUG_VALUE_START(f, var) \
  switch (var) { \
    /* NOLINT(whitespace/blank_line) */

#define DEBUG_VALUE_VALUE(f, var, value) \
    case value: \
      fprintf(f, #value); \
      break;

#define DEBUG_VALUE_END_OCT(f, var) \
    default: \
      fprintf(f, "0%o", var); \
  }

#define DEBUG_VALUE_END_DEC(f, var) \
    default: \
      fprintf(f, "%d", var); \
  }

#define DEBUG_VALUE_END_HEX(f, var) \
    default: \
      fprintf(f, "0x%X", var); \
  }

/**
 * Debug-print O_* flags, as usually seen in the 'flags' parameter of dup3(), open(), pipe2(),
 * posix_spawn_file_actions_addopen() etc. calls.
 */
void debug_open_flags(FILE *f, int flags) {
  DEBUG_BITMAP_START(f, flags)

  int accmode = flags & O_ACCMODE;
  DEBUG_VALUE_START(f, accmode)
  DEBUG_VALUE_VALUE(f, accmode, O_RDONLY);
  DEBUG_VALUE_VALUE(f, accmode, O_WRONLY);
  DEBUG_VALUE_VALUE(f, accmode, O_RDWR);
  DEBUG_VALUE_END_OCT(f, accmode)

  flags &= ~O_ACCMODE;
  sep = "|";

  DEBUG_BITMAP_FLAG(f, flags, O_APPEND)
  DEBUG_BITMAP_FLAG(f, flags, O_ASYNC)
  DEBUG_BITMAP_FLAG(f, flags, O_CLOEXEC)
  DEBUG_BITMAP_FLAG(f, flags, O_CREAT)
#ifdef O_DIRECT
  DEBUG_BITMAP_FLAG(f, flags, O_DIRECT)
#endif
  DEBUG_BITMAP_FLAG(f, flags, O_DIRECTORY)
  DEBUG_BITMAP_FLAG(f, flags, O_DSYNC)
  DEBUG_BITMAP_FLAG(f, flags, O_EXCL)
#ifdef O_LARGEFILE
  DEBUG_BITMAP_FLAG(f, flags, O_LARGEFILE)
#endif
#ifdef O_NOATIME
  DEBUG_BITMAP_FLAG(f, flags, O_NOATIME)
#endif
  DEBUG_BITMAP_FLAG(f, flags, O_NOCTTY)
  DEBUG_BITMAP_FLAG(f, flags, O_NOFOLLOW)
  DEBUG_BITMAP_FLAG(f, flags, O_NONBLOCK)
#ifdef O_PATH
  DEBUG_BITMAP_FLAG(f, flags, O_PATH)
#endif
  DEBUG_BITMAP_FLAG(f, flags, O_SYNC)
#ifdef O_TMPFILE
  DEBUG_BITMAP_FLAG(f, flags, O_TMPFILE)
#endif
  DEBUG_BITMAP_FLAG(f, flags, O_TRUNC)
  DEBUG_BITMAP_END_HEX(f, flags)
}

/**
 * Debug-print AT_* flags, as usually seen in the 'flags' parameter of execveat(), faccessat(),
 * fchmodat(), fchownat(), fstatat(), linkat(), statx(), unlinkat(), utimensat() etc. calls.
 */
void debug_at_flags(FILE *f, int flags) {
  DEBUG_BITMAP_START(f, flags)
  /* AT_EACCESS has different semantics but the same value as AT_REMOVEDIR.
   * FIXME Print whichever semantically matches the current context. */
  /* DEBUG_BITMAP_FLAG(f, flags, AT_EACCESS) */
#ifdef AT_EMPTY_PATH
  DEBUG_BITMAP_FLAG(f, flags, AT_EMPTY_PATH)
#endif
#ifdef AT_NO_AUTOMOUNT
  DEBUG_BITMAP_FLAG(f, flags, AT_NO_AUTOMOUNT)
#endif
#ifdef AT_RECURSIVE
  DEBUG_BITMAP_FLAG(f, flags, AT_RECURSIVE)
#endif
  DEBUG_BITMAP_FLAG(f, flags, AT_REMOVEDIR)
#ifdef AT_STATX_DONT_SYNC
  DEBUG_BITMAP_FLAG(f, flags, AT_STATX_DONT_SYNC)
#endif
#ifdef AT_STATX_FORCE_SYNC
  DEBUG_BITMAP_FLAG(f, flags, AT_STATX_FORCE_SYNC)
#endif
#ifdef AT_STATX_SYNC_AS_STAT
  DEBUG_BITMAP_FLAG(f, flags, AT_STATX_SYNC_AS_STAT)
#endif
#ifdef AT_STATX_SYNC_TYPE
  DEBUG_BITMAP_FLAG(f, flags, AT_STATX_SYNC_TYPE)
#endif
  DEBUG_BITMAP_FLAG(f, flags, AT_SYMLINK_FOLLOW)
  DEBUG_BITMAP_FLAG(f, flags, AT_SYMLINK_NOFOLLOW)
  DEBUG_BITMAP_END_HEX(f, flags)
}

/**
 * Debug spawn‐flags attribute (set using posix_spawnattr_setflags(3)).
 */
void debug_psfa_attr_flags(FILE *f, int flags) {
  DEBUG_BITMAP_START(f, flags)
#ifdef POSIX_SPAWN_RESETIDS
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_RESETIDS)
#endif
#ifdef POSIX_SPAWN_SETPGROUP
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_SETPGROUP)
#endif
#ifdef POSIX_SPAWN_SETSIGDEF
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_SETSIGDEF)
#endif
#ifdef POSIX_SPAWN_SETSIGMASK
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_SETSIGMASK)
#endif
#ifdef POSIX_SPAWN_SETEXEC
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_SETEXEC)
#endif
#ifdef POSIX_SPAWN_START_SUSPENDED
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_START_SUSPENDED)
#endif
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  DEBUG_BITMAP_FLAG(f, flags, POSIX_SPAWN_CLOEXEC_DEFAULT)
#endif
  DEBUG_BITMAP_END_HEX(f, flags)
}

/**
 * Debug-print the 'cmdd' parameter of an fcntl() call.
 */
void debug_fcntl_cmd(FILE *f, int cmd) {
  DEBUG_VALUE_START(f, cmd)
  DEBUG_VALUE_VALUE(f, cmd, F_DUPFD)
  DEBUG_VALUE_VALUE(f, cmd, F_DUPFD_CLOEXEC)
  DEBUG_VALUE_VALUE(f, cmd, F_GETFD)
  DEBUG_VALUE_VALUE(f, cmd, F_SETFD)
  DEBUG_VALUE_VALUE(f, cmd, F_GETFL)
  DEBUG_VALUE_VALUE(f, cmd, F_SETFL)
  DEBUG_VALUE_VALUE(f, cmd, F_GETLK)
  DEBUG_VALUE_VALUE(f, cmd, F_SETLK)
  DEBUG_VALUE_VALUE(f, cmd, F_SETLKW)
  DEBUG_VALUE_VALUE(f, cmd, F_GETOWN)
  DEBUG_VALUE_VALUE(f, cmd, F_SETOWN)
#ifdef F_GETOWN_EX
  DEBUG_VALUE_VALUE(f, cmd, F_GETOWN_EX)
#endif
#ifdef F_SETOWN_EX
  DEBUG_VALUE_VALUE(f, cmd, F_SETOWN_EX)
#endif
#ifdef F_GETSIG
  DEBUG_VALUE_VALUE(f, cmd, F_GETSIG)
#endif
#ifdef F_SETSIG
  DEBUG_VALUE_VALUE(f, cmd, F_SETSIG)
#endif
#ifdef F_GETLEASE
  DEBUG_VALUE_VALUE(f, cmd, F_GETLEASE)
#endif
#ifdef F_SETLEASE
  DEBUG_VALUE_VALUE(f, cmd, F_SETLEASE)
#endif
#ifdef F_NOTIFY
  DEBUG_VALUE_VALUE(f, cmd, F_NOTIFY)
#endif
#ifdef F_GETPIPE_SZ
  DEBUG_VALUE_VALUE(f, cmd, F_GETPIPE_SZ)
#endif
#ifdef F_SETPIPE_SZ
  DEBUG_VALUE_VALUE(f, cmd, F_SETPIPE_SZ)
#endif
#ifdef F_ADD_SEALS
  DEBUG_VALUE_VALUE(f, cmd, F_ADD_SEALS)
#endif
#ifdef F_GET_SEALS
  DEBUG_VALUE_VALUE(f, cmd, F_GET_SEALS)
#endif
#ifdef F_GET_RW_HINT
  DEBUG_VALUE_VALUE(f, cmd, F_GET_RW_HINT)
#endif
#ifdef F_SET_RW_HINT
  DEBUG_VALUE_VALUE(f, cmd, F_SET_RW_HINT)
#endif
#ifdef F_GET_FILE_RW_HINT
  DEBUG_VALUE_VALUE(f, cmd, F_GET_FILE_RW_HINT)
#endif
#ifdef F_SET_FILE_RW_HINT
  DEBUG_VALUE_VALUE(f, cmd, F_SET_FILE_RW_HINT)
#endif
#ifdef F_GETPATH
  DEBUG_VALUE_VALUE(f, cmd, F_GETPATH)
#endif
  DEBUG_VALUE_END_DEC(f, cmd)
}

/**
 * Debug-print fcntl()'s 'arg'parameter or return value. The debugging format depends on 'cmd'.
 */
void debug_fcntl_arg_or_ret(FILE *f, int cmd, int arg_or_ret) {
  switch (cmd) {
    case F_GETFD:
    case F_SETFD:
      if (arg_or_ret) {
        DEBUG_BITMAP_START(f, arg_or_ret)
        DEBUG_BITMAP_FLAG(f, arg_or_ret, FD_CLOEXEC)
        DEBUG_BITMAP_END_HEX(f, arg_or_ret)
      } else {
        fprintf(f, "0");
      }
      break;
    case F_GETFL:
    case F_SETFL:
      debug_open_flags(f, arg_or_ret);
      break;
    default:
      fprintf(f, "%d", arg_or_ret);
  }
}

/**
 * Debug-print socket()'s 'domain' parameter.
 */
void debug_socket_domain(FILE *f, int domain) {
  DEBUG_VALUE_START(f, domain)
  DEBUG_VALUE_VALUE(f, domain, AF_UNIX)
  DEBUG_VALUE_VALUE(f, domain, AF_INET)
  DEBUG_VALUE_VALUE(f, domain, AF_APPLETALK)
  DEBUG_VALUE_VALUE(f, domain, AF_INET6)
#ifdef AF_KEY
  DEBUG_VALUE_VALUE(f, domain, AF_KEY)
#endif
#ifdef AF_NETLINK
  DEBUG_VALUE_VALUE(f, domain, AF_NETLINK)
#endif
#ifdef AF_PACKET
  DEBUG_VALUE_VALUE(f, domain, AF_PACKET)
#endif
  DEBUG_VALUE_END_DEC(f, domain)
}

/**
 * Debug-print an error number.
 */
void debug_error_no(FILE *f, int error_no) {
  // FIXME: glibc 2.32 adds strerrorname_np(), switch to that one day.
  DEBUG_VALUE_START(f, error_no)
  DEBUG_VALUE_VALUE(f, error_no, E2BIG)
  DEBUG_VALUE_VALUE(f, error_no, EACCES)
  DEBUG_VALUE_VALUE(f, error_no, EADDRINUSE)
  DEBUG_VALUE_VALUE(f, error_no, EADDRNOTAVAIL)
#ifdef EADV
  DEBUG_VALUE_VALUE(f, error_no, EADV)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EAFNOSUPPORT)
  DEBUG_VALUE_VALUE(f, error_no, EAGAIN)
  DEBUG_VALUE_VALUE(f, error_no, EALREADY)
#ifdef EBADE
  DEBUG_VALUE_VALUE(f, error_no, EBADE)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EBADF)
#ifdef EBADFD
  DEBUG_VALUE_VALUE(f, error_no, EBADFD)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EBADMSG)
#ifdef EBADR
  DEBUG_VALUE_VALUE(f, error_no, EBADR)
#endif
#ifdef EBADRQC
  DEBUG_VALUE_VALUE(f, error_no, EBADRQC)
#endif
#ifdef EBADSLT
  DEBUG_VALUE_VALUE(f, error_no, EBADSLT)
#endif
#ifdef EBFONT
  DEBUG_VALUE_VALUE(f, error_no, EBFONT)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EBUSY)
  DEBUG_VALUE_VALUE(f, error_no, ECANCELED)
  DEBUG_VALUE_VALUE(f, error_no, ECHILD)
#ifdef ECHRNG
  DEBUG_VALUE_VALUE(f, error_no, ECHRNG)
#endif
#ifdef ECOMM
  DEBUG_VALUE_VALUE(f, error_no, ECOMM)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ECONNABORTED)
  DEBUG_VALUE_VALUE(f, error_no, ECONNREFUSED)
  DEBUG_VALUE_VALUE(f, error_no, ECONNRESET)
  DEBUG_VALUE_VALUE(f, error_no, EDEADLK)
  /* DEBUG_VALUE_VALUE(f, error_no, EDEADLOCK) - same as EDEADLK on Linux */
  DEBUG_VALUE_VALUE(f, error_no, EDESTADDRREQ)
  DEBUG_VALUE_VALUE(f, error_no, EDOM)
#ifdef EDOTDOT
  DEBUG_VALUE_VALUE(f, error_no, EDOTDOT)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EDQUOT)
  DEBUG_VALUE_VALUE(f, error_no, EEXIST)
  DEBUG_VALUE_VALUE(f, error_no, EFAULT)
  DEBUG_VALUE_VALUE(f, error_no, EFBIG)
  DEBUG_VALUE_VALUE(f, error_no, EHOSTDOWN)
  DEBUG_VALUE_VALUE(f, error_no, EHOSTUNREACH)
#ifdef EHWPOISON
  DEBUG_VALUE_VALUE(f, error_no, EHWPOISON)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EIDRM)
  DEBUG_VALUE_VALUE(f, error_no, EILSEQ)
  DEBUG_VALUE_VALUE(f, error_no, EINPROGRESS)
  DEBUG_VALUE_VALUE(f, error_no, EINTR)
  DEBUG_VALUE_VALUE(f, error_no, EINVAL)
  DEBUG_VALUE_VALUE(f, error_no, EIO)
  DEBUG_VALUE_VALUE(f, error_no, EISCONN)
  DEBUG_VALUE_VALUE(f, error_no, EISDIR)
#ifdef EISNAM
  DEBUG_VALUE_VALUE(f, error_no, EISNAM)
#endif
#ifdef EKEYEXPIRED
  DEBUG_VALUE_VALUE(f, error_no, EKEYEXPIRED)
#endif
#ifdef EKEYREJECTED
  DEBUG_VALUE_VALUE(f, error_no, EKEYREJECTED)
#endif
#ifdef EKEYREVOKED
  DEBUG_VALUE_VALUE(f, error_no, EKEYREVOKED)
#endif
#ifdef EL2HLT
  DEBUG_VALUE_VALUE(f, error_no, EL2HLT)
#endif
#ifdef EL2NSYNC
  DEBUG_VALUE_VALUE(f, error_no, EL2NSYNC)
#endif
#ifdef EL3HLT
  DEBUG_VALUE_VALUE(f, error_no, EL3HLT)
#endif
#ifdef EL3RST
  DEBUG_VALUE_VALUE(f, error_no, EL3RST)
#endif
#ifdef ELIBACC
  DEBUG_VALUE_VALUE(f, error_no, ELIBACC)
#endif
#ifdef ELIBBAD
  DEBUG_VALUE_VALUE(f, error_no, ELIBBAD)
#endif
#ifdef ELIBEXEC
  DEBUG_VALUE_VALUE(f, error_no, ELIBEXEC)
#endif
#ifdef ELIBMAX
  DEBUG_VALUE_VALUE(f, error_no, ELIBMAX)
#endif
#ifdef ELIBSCN
  DEBUG_VALUE_VALUE(f, error_no, ELIBSCN)
#endif
#ifdef ELNRNG
  DEBUG_VALUE_VALUE(f, error_no, ELNRNG)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ELOOP)
#ifdef EMEDIUMTYPE
  DEBUG_VALUE_VALUE(f, error_no, EMEDIUMTYPE)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EMFILE)
  DEBUG_VALUE_VALUE(f, error_no, EMLINK)
  DEBUG_VALUE_VALUE(f, error_no, EMSGSIZE)
  DEBUG_VALUE_VALUE(f, error_no, EMULTIHOP)
  DEBUG_VALUE_VALUE(f, error_no, ENAMETOOLONG)
#ifdef ENAVAIL
  DEBUG_VALUE_VALUE(f, error_no, ENAVAIL)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENETDOWN)
  DEBUG_VALUE_VALUE(f, error_no, ENETRESET)
  DEBUG_VALUE_VALUE(f, error_no, ENETUNREACH)
  DEBUG_VALUE_VALUE(f, error_no, ENFILE)
#ifdef ENOANO
  DEBUG_VALUE_VALUE(f, error_no, ENOANO)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENOBUFS)
#ifdef ENOCSI
  DEBUG_VALUE_VALUE(f, error_no, ENOCSI)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENODATA)
  DEBUG_VALUE_VALUE(f, error_no, ENODEV)
  DEBUG_VALUE_VALUE(f, error_no, ENOENT)
  DEBUG_VALUE_VALUE(f, error_no, ENOEXEC)
#ifdef ENOKEY
  DEBUG_VALUE_VALUE(f, error_no, ENOKEY)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENOLCK)
  DEBUG_VALUE_VALUE(f, error_no, ENOLINK)
#ifdef ENOMEDIUM
  DEBUG_VALUE_VALUE(f, error_no, ENOMEDIUM)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENOMEM)
  DEBUG_VALUE_VALUE(f, error_no, ENOMSG)
#ifdef ENONET
  DEBUG_VALUE_VALUE(f, error_no, ENONET)
#endif
#ifdef ENOPKG
  DEBUG_VALUE_VALUE(f, error_no, ENOPKG)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENOPROTOOPT)
  DEBUG_VALUE_VALUE(f, error_no, ENOSPC)
  DEBUG_VALUE_VALUE(f, error_no, ENOSR)
  DEBUG_VALUE_VALUE(f, error_no, ENOSTR)
  DEBUG_VALUE_VALUE(f, error_no, ENOSYS)
  DEBUG_VALUE_VALUE(f, error_no, ENOTBLK)
  DEBUG_VALUE_VALUE(f, error_no, ENOTCONN)
  DEBUG_VALUE_VALUE(f, error_no, ENOTDIR)
  DEBUG_VALUE_VALUE(f, error_no, ENOTEMPTY)
#ifdef ENOTNAM
  DEBUG_VALUE_VALUE(f, error_no, ENOTNAM)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENOTRECOVERABLE)
  DEBUG_VALUE_VALUE(f, error_no, ENOTSOCK)
  DEBUG_VALUE_VALUE(f, error_no, ENOTSUP)
  DEBUG_VALUE_VALUE(f, error_no, ENOTTY)
#ifdef ENOTUNIQ
  DEBUG_VALUE_VALUE(f, error_no, ENOTUNIQ)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ENXIO)
  /* DEBUG_VALUE_VALUE(f, error_no, EOPNOTSUPP) - same as ENOTSUPP on Linux */
  DEBUG_VALUE_VALUE(f, error_no, EOVERFLOW)
  DEBUG_VALUE_VALUE(f, error_no, EOWNERDEAD)
  DEBUG_VALUE_VALUE(f, error_no, EPERM)
  DEBUG_VALUE_VALUE(f, error_no, EPFNOSUPPORT)
  DEBUG_VALUE_VALUE(f, error_no, EPIPE)
  DEBUG_VALUE_VALUE(f, error_no, EPROTO)
  DEBUG_VALUE_VALUE(f, error_no, EPROTONOSUPPORT)
  DEBUG_VALUE_VALUE(f, error_no, EPROTOTYPE)
  DEBUG_VALUE_VALUE(f, error_no, ERANGE)
#ifdef EREMCHG
  DEBUG_VALUE_VALUE(f, error_no, EREMCHG)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EREMOTE)
#ifdef EREMOTEIO
  DEBUG_VALUE_VALUE(f, error_no, EREMOTEIO)
#endif
#ifdef ERESTART
  DEBUG_VALUE_VALUE(f, error_no, ERESTART)
#endif
#ifdef ERFKILL
  DEBUG_VALUE_VALUE(f, error_no, ERFKILL)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EROFS)
  DEBUG_VALUE_VALUE(f, error_no, ESHUTDOWN)
  DEBUG_VALUE_VALUE(f, error_no, ESOCKTNOSUPPORT)
  DEBUG_VALUE_VALUE(f, error_no, ESPIPE)
  DEBUG_VALUE_VALUE(f, error_no, ESRCH)
#ifdef ESRMNT
  DEBUG_VALUE_VALUE(f, error_no, ESRMNT)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ESTALE)
#ifdef ESTRPIPE
  DEBUG_VALUE_VALUE(f, error_no, ESTRPIPE)
#endif
  DEBUG_VALUE_VALUE(f, error_no, ETIME)
  DEBUG_VALUE_VALUE(f, error_no, ETIMEDOUT)
  DEBUG_VALUE_VALUE(f, error_no, ETOOMANYREFS)
  DEBUG_VALUE_VALUE(f, error_no, ETXTBSY)
#ifdef EUCLEAN
  DEBUG_VALUE_VALUE(f, error_no, EUCLEAN)
#endif
#ifdef EUNATCH
  DEBUG_VALUE_VALUE(f, error_no, EUNATCH)
#endif
  DEBUG_VALUE_VALUE(f, error_no, EUSERS)
  /* DEBUG_VALUE_VALUE(f, error_no, EWOULDBLOCK) - same as EAGAIN on Linux */
  DEBUG_VALUE_VALUE(f, error_no, EXDEV)
#ifdef EXFULL
  DEBUG_VALUE_VALUE(f, error_no, EXFULL)
#endif
  DEBUG_VALUE_END_DEC(f, error_no)
}

/**
 * Debug-print a signal number.
 */
void debug_signum(FILE *f, int signum) {
  DEBUG_VALUE_START(f, signum);
  DEBUG_VALUE_VALUE(f, signum, SIGHUP);
  DEBUG_VALUE_VALUE(f, signum, SIGINT);
  DEBUG_VALUE_VALUE(f, signum, SIGQUIT);
  DEBUG_VALUE_VALUE(f, signum, SIGILL);
  DEBUG_VALUE_VALUE(f, signum, SIGTRAP);
  DEBUG_VALUE_VALUE(f, signum, SIGABRT);
  DEBUG_VALUE_VALUE(f, signum, SIGBUS);
  DEBUG_VALUE_VALUE(f, signum, SIGFPE);
  DEBUG_VALUE_VALUE(f, signum, SIGKILL);
  DEBUG_VALUE_VALUE(f, signum, SIGUSR1);
  DEBUG_VALUE_VALUE(f, signum, SIGSEGV);
  DEBUG_VALUE_VALUE(f, signum, SIGUSR2);
  DEBUG_VALUE_VALUE(f, signum, SIGPIPE);
  DEBUG_VALUE_VALUE(f, signum, SIGALRM);
  DEBUG_VALUE_VALUE(f, signum, SIGTERM);
#ifdef SIGSTKFLT
  DEBUG_VALUE_VALUE(f, signum, SIGSTKFLT);
#endif
  DEBUG_VALUE_VALUE(f, signum, SIGCHLD);
  DEBUG_VALUE_VALUE(f, signum, SIGCONT);
  DEBUG_VALUE_VALUE(f, signum, SIGSTOP);
  DEBUG_VALUE_VALUE(f, signum, SIGTSTP);
  DEBUG_VALUE_VALUE(f, signum, SIGTTIN);
  DEBUG_VALUE_VALUE(f, signum, SIGTTOU);
  DEBUG_VALUE_VALUE(f, signum, SIGURG);
  DEBUG_VALUE_VALUE(f, signum, SIGXCPU);
  DEBUG_VALUE_VALUE(f, signum, SIGXFSZ);
  DEBUG_VALUE_VALUE(f, signum, SIGVTALRM);
  DEBUG_VALUE_VALUE(f, signum, SIGPROF);
  DEBUG_VALUE_VALUE(f, signum, SIGWINCH);
  DEBUG_VALUE_VALUE(f, signum, SIGIO);
#ifdef SIGPWR
  DEBUG_VALUE_VALUE(f, signum, SIGPWR);
#endif
  DEBUG_VALUE_VALUE(f, signum, SIGSYS);
  DEBUG_VALUE_END_DEC(f, signum);
}

/**
 * Debug-print a mode_t variable.
 *
 * mode_t sometimes contains the file type (e.g. when returned by a stat() call) and sometimes
 * doesn't (e.g. when it's a parameter to an open(), chmod(), umask() call).
 *
 * Luckily, at least on Linux, none of the S_IF* constants are defined as 0. This means that we can
 * determine which category we fall into and we can produce nice debug output in both cases, without
 * having to maintain two separate functions.
 */
void debug_mode_t(FILE *f, mode_t mode) {
  const char *sep = "|";

  mode_t type = mode & S_IFMT;
  DEBUG_VALUE_START(f, type)
  DEBUG_VALUE_VALUE(f, type, S_IFREG)
  DEBUG_VALUE_VALUE(f, type, S_IFDIR)
  DEBUG_VALUE_VALUE(f, type, S_IFLNK)
  DEBUG_VALUE_VALUE(f, type, S_IFBLK)
  DEBUG_VALUE_VALUE(f, type, S_IFCHR)
  DEBUG_VALUE_VALUE(f, type, S_IFIFO)
  DEBUG_VALUE_VALUE(f, type, S_IFSOCK)
  case 0:
    /* File type info is not available. Don't print anything here. */
    sep = "";
    break;
  DEBUG_VALUE_END_OCT(f, type)

  mode &= ~S_IFMT;
  fprintf(f, "%s0%03o", sep, mode);
}

/*
 * Debug-print a "wait status", as usually seen in the non-error return value of system() and
 * pclose(), and in the "wstatus" out parameter of the wait*() family.
 */
void debug_wstatus(FILE *f, int wstatus) {
  const char *sep = "";
  fprintf(f, "%d (", wstatus);
  if (WIFEXITED(wstatus)) {
    fprintf(f, "%sexitstatus=%d", sep, WEXITSTATUS(wstatus));
    sep = ", ";
  }
  if (WIFSIGNALED(wstatus)) {
    fprintf(f, "%stermsig=", sep);
    debug_signum(f, WTERMSIG(wstatus));
    if (WCOREDUMP(wstatus)) {
      fprintf(f, ", coredump");
    }
    sep = ", ";
  }
  if (WIFSTOPPED(wstatus)) {
    fprintf(f, "%sstopsig=", sep);
    debug_signum(f, WTERMSIG(wstatus));
    sep = ", ";
  }
  if (WIFCONTINUED(wstatus)) {
    fprintf(f, "%scontinued", sep);
    sep = ", ";
  }
  fprintf(f, ")");
}

/**
 * Debug-print CLONE_* flags, as usually seen in the 'flags' parameter of clone().
 */
void debug_clone_flags(FILE *f, int flags) {
  DEBUG_BITMAP_START(f, flags);
/* If CLONE_VM is not defined most likely other flags are not defined either. */
#ifdef CLONE_VM
  DEBUG_BITMAP_FLAG(f, flags, CLONE_VM);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_FS);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_FILES);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_SIGHAND);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_PIDFD);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_PTRACE);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_VFORK);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_PARENT);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_THREAD);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWNS);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_SYSVSEM);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_SETTLS);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_PARENT_SETTID);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_CHILD_CLEARTID);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_DETACHED);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_UNTRACED);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_CHILD_SETTID);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWCGROUP);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWUTS);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWIPC);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWUSER);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWPID);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_NEWNET);
  DEBUG_BITMAP_FLAG(f, flags, CLONE_IO);
#endif
  DEBUG_BITMAP_END_HEX(f, flags & ~0xff);
  fprintf(f, "|");
  debug_signum(f, flags & 0xff);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif
