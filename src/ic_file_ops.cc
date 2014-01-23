/* from fcntl.h */

#include <fcntl.h>
#include <mntent.h>
#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <link.h>
#include <sys/resource.h>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

#include "intercept.h"
#include "fb-messages.pb.h"

namespace firebuild {

typedef char* CHARS;
typedef void* VOIDPT;

#define IC2_MSG_IC2_WITH_RET  m->set_ret(ret)
#define IC2_MSG_IC2_NO_RET

#define IC2_ERR_VAL_int -1
#define IC2_ERR_VAL_long -1
#define IC2_ERR_VAL_CHARS NULL
#define IC2_ERR_VAL_VOIDPT NULL

#define IC2_WAIT_ACK while (0) {}

#define IC2_SIMPLE_NP(ics_rettype, ics_with_rettype,  ics_pmtype,   \
                      ics_pmname, ics_pars, ics_body)               \
  static void                                                       \
  intercept_##ics_pmname ics_pars                                   \
  {                                                                 \
    if (fb_exec_called || fb_exit_handled) {                        \
      /* No message is sent during exec to prevent sending many */  \
      /* automatic close()-s. */                                    \
      return;                                                       \
    }                                                               \
    msg::InterceptorMsg ic_msg;                                     \
    int saved_errno = errno;                                        \
                                                                    \
    auto m = ic_msg.mutable_##ics_pmname();                         \
    ics_body;                                                       \
    IC2_MSG_##ics_with_rettype;                                     \
    if (ret == IC2_ERR_VAL_##ics_rettype) {                         \
      m->set_error_no(saved_errno);                                 \
    }                                                               \
    fb_send_msg(ic_msg, fb_sv_conn);                                \
    IC2_WAIT_ACK;                                                   \
    errno = saved_errno;                                            \
  }

#define IC2_SIMPLE_0P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname)                                       \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_rettype ret), {})

#define IC2_SIMPLE_1P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname,                                       \
                      ics_ptype1, ics_pmattrname1)                      \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_ptype1 ics_p1, ics_rettype ret),                   \
                {m->set_##ics_pmattrname1(ics_p1);})

#define IC2_SIMPLE_2P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname,                                       \
                      ics_ptype1, ics_pmattrname1,                      \
                      ics_ptype2, ics_pmattrname2)                      \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_ptype1 ics_p1, ics_ptype2 ics_p2,                  \
                 ics_rettype ret),                                      \
                {                                                       \
                  m->set_##ics_pmattrname1(ics_p1);                     \
                  m->set_##ics_pmattrname2(ics_p2);                     \
                })

#define IC2_SIMPLE_3P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname,                                       \
                      ics_ptype1, ics_pmattrname1,                      \
                      ics_ptype2, ics_pmattrname2,                      \
                      ics_ptype3, ics_pmattrname3)                      \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_ptype1 ics_p1, ics_ptype2 ics_p2,                  \
                 ics_ptype3 ics_p3, ics_rettype ret),                   \
                {                                                       \
                  m->set_##ics_pmattrname1(ics_p1);                     \
                  m->set_##ics_pmattrname2(ics_p2);                     \
                  m->set_##ics_pmattrname3(ics_p3);                     \
                })

#define IC2_SIMPLE_4P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname,                                       \
                      ics_ptype1, ics_pmattrname1,                      \
                      ics_ptype2, ics_pmattrname2,                      \
                      ics_ptype3, ics_pmattrname3,                      \
                      ics_ptype4, ics_pmattrname4)                      \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_ptype1 ics_p1, ics_ptype2 ics_p2,                  \
                 ics_ptype3 ics_p3, ics_ptype4 ics_p4,                  \
                 ics_rettype ret),                                      \
                {                                                       \
                  m->set_##ics_pmattrname1(ics_p1);                     \
                  m->set_##ics_pmattrname2(ics_p2);                     \
                  m->set_##ics_pmattrname3(ics_p3);                     \
                  m->set_##ics_pmattrname4(ics_p4);                     \
                })

#define IC2_SIMPLE_5P(ics_rettype, ics_with_rettype, ics_pmtype,        \
                      ics_pmname,                                       \
                      ics_ptype1, ics_pmattrname1,                      \
                      ics_ptype2, ics_pmattrname2,                      \
                      ics_ptype3, ics_pmattrname3,                      \
                      ics_ptype4, ics_pmattrname4,                      \
                      ics_ptype5, ics_pmattrname5)                      \
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,  \
                (ics_ptype1 ics_p1, ics_ptype2 ics_p2,                  \
                 ics_ptype3 ics_p3, ics_ptype4 ics_p4,                  \
                 ics_ptype5 ics_p5, ics_rettype ret),                   \
                {                                                       \
                  m->set_##ics_pmattrname1(ics_p1);                     \
                  m->set_##ics_pmattrname2(ics_p2);                     \
                  m->set_##ics_pmattrname3(ics_p3);                     \
                  m->set_##ics_pmattrname4(ics_p4);                     \
                  m->set_##ics_pmattrname5(ics_p5);                     \
                })

/* Intercept unlink */
IC2_SIMPLE_1P(int, IC2_NO_RET, UnLink, unlink, const char *, path)
/* Intercept unlinkat */
IC2_SIMPLE_3P(int, IC2_NO_RET, UnLinkAt, unlinkat, int, dirfd, const char *, pathname, int, flags)
/* Intercept fchdir */
IC2_SIMPLE_1P(int, IC2_NO_RET, FChDir, fchdir, const int, dir)
/* Intercept fcloseall */
IC2_SIMPLE_0P(int, IC2_NO_RET, FCloseAll, fcloseall)
/* Intercept rmdir */
IC2_SIMPLE_1P(int, IC2_NO_RET, RmDir, rmdir, const char *, dir)
/* Intercept chown */
IC2_SIMPLE_3P(int, IC2_NO_RET, Chown, chown, const char *, path, uid_t, owner, gid_t, group)
/* Intercept fchown */
IC2_SIMPLE_3P(int, IC2_NO_RET, FChown, fchown, int, fd, uid_t, owner, gid_t, group)
/* Intercept fchownat */
IC2_SIMPLE_5P(int, IC2_NO_RET, FChownAt, fchownat, int, dirfd, const char *, path, uid_t,
              owner, gid_t, group, int, flags)
/* Intercept lchown */
IC2_SIMPLE_3P(int, IC2_NO_RET, LChown, lchown, const char *, path, uid_t, owner, gid_t, group)
/* Intercept link */
IC2_SIMPLE_2P(int, IC2_NO_RET, Link, link, const char *, oldpath, const char *, newpath)
/* Intercept linkat */
IC2_SIMPLE_5P(int, IC2_NO_RET, LinkAt, linkat, int, olddirfd, const char *, oldpath, int,
              newdirfd, const char *, newpath, int, flags)
/* Intercept symlink */
IC2_SIMPLE_2P(int, IC2_NO_RET, Symlink, symlink, const char *, oldpath, const char *, newpath)
/* Intercept symlinkat */
IC2_SIMPLE_3P(int, IC2_NO_RET, SymlinkAt, symlinkat, const char *, oldpath, int, newdirfd, const char *, newpath)
/* Intercept lockf (without offset)*/
IC2_SIMPLE_2P(int, IC2_NO_RET, LockF, lockf, int, fd, int, cmd)
/* Intercept fcntl with arg */
IC2_SIMPLE_3P(int, IC2_WITH_RET, Fcntl, fcntl, int, fd, int, cmd, int, arg)
/* Intercept fcntl without arg */
IC2_SIMPLE_2P(int, IC2_WITH_RET, Fcntl, fcntl, int, fd, int, cmd)
/* Intercept getcwd */
IC2_SIMPLE_0P(CHARS, IC2_WITH_RET, GetCwd, getcwd)
/* Intercept sysconf */
IC2_SIMPLE_1P(long, IC2_WITH_RET, Sysconf, sysconf, int, name)
/* Intercept syscall */
IC2_SIMPLE_1P(long, IC2_WITH_RET, SysCall, syscall, int, number)
/* Intercept dup */
IC2_SIMPLE_1P(int, IC2_WITH_RET, Dup, dup, int, oldfd)
/* Intercept dup */
IC2_SIMPLE_3P(int, IC2_WITH_RET, Dup3, dup3, int, oldfd, int, newfd, int, flags)

/* Intercept readlink */
IC2_SIMPLE_2P(int, IC2_NO_RET, ReadLink, readlink, const char *, path, const char *, ret_path)
/* Intercept readlinkat */
IC2_SIMPLE_3P(int, IC2_NO_RET, ReadLinkAt, readlinkat, int, dirfd, const char *, path, const char *, ret_path)

/** Frontend for intercept_readlink and intercept_readlinkat */
static void
intercept_readlink_helper(const int fd, const char *path, const char * const buf,
                          const ssize_t len, const ssize_t ret)
{
  char *ret_path;
  if ((ret >= 0) && (abs(ret) <= len)) {
    ret_path = strndup(buf, ret);
  } else {
    ret_path = strdup("");
  }
  if (fd != -1 ) {
    intercept_readlink(path, ret_path, ret);
  } else {
    intercept_readlinkat(fd, path, ret_path, ret);
  }
  free(ret_path);
}

/* Intercept remove */
IC2_SIMPLE_1P(int, IC2_NO_RET, Remove, remove, const char *, filename)
/* Intercept rename */
IC2_SIMPLE_2P(int, IC2_NO_RET, Rename, rename, const char *, oldpath, const char *, newpath)
/* Intercept renameat */
IC2_SIMPLE_4P(int, IC2_NO_RET, RenameAt, renameat, int, oldfd, const char *, oldpath, int, newfd, const char *, newpath)

/* Intercept access */
IC2_SIMPLE_2P(int, IC2_NO_RET, Access, access, const char *, pathname, int, mode)
/* Intercept eaccess */
IC2_SIMPLE_2P(int, IC2_NO_RET, EAccess, eaccess, const char *, pathname, int, mode)
/* Intercept faccessat */
IC2_SIMPLE_4P(int, IC2_NO_RET, FAccessAt, faccessat, int, dirfd, const char *, pathname, int, mode, int, flags)
/* Intercept (l)utime(s) */
IC2_SIMPLE_3P(int, IC2_NO_RET, UTime, utime, int, at, const char *, file, bool, link)
/* Intercept futimes */
IC2_SIMPLE_1P(int, IC2_NO_RET, FUTime, futime, int, fd)

/* Intercept dlopen */
IC2_SIMPLE_2P(VOIDPT, IC2_NO_RET, DLOpen, dlopen, const char *, filename, int, flag)

/* Intercept pipe variants */
static void
intercept_pipe2 (const int pipefd[2], const int flags, const int ret)
{
  msg::InterceptorMsg ic_msg;
  int saved_errno = errno;

  auto m = ic_msg.mutable_pipe2();
  m->set_pipefd0(pipefd[0]);
  m->set_pipefd0(pipefd[1]);
  m->set_flags(flags);
  if (ret == -1) {
    m->set_error_no(saved_errno);
  }

  fb_send_msg(ic_msg, fb_sv_conn);
  errno = saved_errno;
}


static void
intercept_execve (const bool with_p, const char * const file, const int fd,
                  const char *const argv[], const char *const envp[])
{
  msg::InterceptorMsg ic_msg;
  msg::SupervisorMsg sv_msg;
  struct rusage ru;

  auto m = ic_msg.mutable_execv();
  if (with_p) {
    m->set_with_p(with_p);
  }
  if ((file != NULL) && (fd == -1)) {
    m->set_file(file);
  } else {
    m->set_fd(fd);
  }
  for (int i = 0; argv[i] != NULL; i++) {
    m->add_arg(argv[i]);
  }
  for (int i = 0; envp[i] != NULL; i++) {
    m->add_env(envp[i]);
  }
  if (fd == -1){
    char * tmp_path;
    if ((tmp_path = getenv("PATH"))) {
      m->set_path(tmp_path);
    } else {
      /* we have to fall back as described in man execvp */
      char cwd_buf[CWD_BUFSIZE];
      size_t n = ic_orig_confstr(_CS_PATH, NULL, 0);
      char *cs_path = new char [n];
      assert (cs_path != NULL);
      ic_orig_confstr(_CS_PATH, cs_path, n);
      ic_orig_getcwd(cwd_buf, CWD_BUFSIZE);
      n = snprintf(NULL, 0, "%s:%s", cwd_buf, cs_path);
      tmp_path = new char[n + 1];
      snprintf(tmp_path, n+1, "%s:%s", cwd_buf, cs_path);
      m->set_path(tmp_path);
      delete[] tmp_path;
      delete[] cs_path;
    }
  }

  // get CPU time used up to this exec()
  getrusage(RUSAGE_SELF, &ru);
  m->set_utime_m(ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000);
  m->set_stime_m(ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000);

  fb_send_msg(ic_msg, fb_sv_conn);
  fb_recv_msg(sv_msg, fb_sv_conn);
  if (!sv_msg.ack()) {
    // something unexpected happened ...
    assert(0 && "Interceptor has not received ACK from firebuild");
  }
  fb_exec_called = true;
}
/* Intercept failed (f)execv*() */
IC2_SIMPLE_1P(int, IC2_NO_RET, ExecVFailed, execvfailed, int, pid)

static void intercept_execvfailed2(int pid, int ret)
{
  fb_exec_called = false;
  intercept_execvfailed(pid, ret);
}

/* Intercept gethostname */
IC2_SIMPLE_2P(int, IC2_NO_RET, GetHostname, gethostname, const char *, name, size_t, len)
/* Intercept getdomainname */
IC2_SIMPLE_2P(int, IC2_NO_RET, GetDomainname, getdomainname, const char *, name, size_t, len)
/* Intercept truncate(64) */
IC2_SIMPLE_2P(int, IC2_NO_RET, Truncate, truncate, const char *, path, off64_t, len)
/* Intercept ftruncate(64) */
IC2_SIMPLE_2P(int, IC2_NO_RET, FTruncate, ftruncate, int, fd, off64_t, len)
/* Intercept pathconf */
IC2_SIMPLE_2P(long, IC2_WITH_RET, PathConf, pathconf, const char *, path, int, name)
/* Intercept fpathconf */
IC2_SIMPLE_2P(long, IC2_WITH_RET, FPathConf, fpathconf, int, fd, int, name)
/* Intercept fopen */
IC2_SIMPLE_2P(int, IC2_WITH_RET, FOpen, fopen, const char *, filename, const char *, modes)
/* Intercept freopen */
IC2_SIMPLE_3P(int, IC2_WITH_RET, FReOpen, freopen, const char *, filename, const char *, modes, int, fd)

// macro generated interceptor functions below require ACK from supervisor
#undef IC2_WAIT_ACK
#define IC2_WAIT_ACK while (0) {                                        \
    msg::SupervisorMsg sv_msg;                                          \
    if (( 0 >= fb_recv_msg(sv_msg, fb_sv_conn)) || !sv_msg.ack()) {     \
                                                                        \
      /* something unexpected happened ... */                           \
      assert(0 && "Interceptor has not received ACK from firebuild");   \
    }                                                                   \
  }

/* Intercept open variants */
IC2_SIMPLE_3P(int, IC2_WITH_RET, Open, open, const char *, file, const int, flags, const int, mode)
/* Intercept open variants creating the file*/
IC2_SIMPLE_4P(int, IC2_WITH_RET, Open, open, const char *, file, const int, flags, const int, mode, bool, created)
/* Intercept close */
IC2_SIMPLE_1P(int, IC2_NO_RET, Close, close, const int, fd)
/* Intercept opendir */
IC2_SIMPLE_1P(VOIDPT, IC2_NO_RET, OpenDir, opendir, const char *, name)
/* Intercept fdopendir */
IC2_SIMPLE_1P(VOIDPT, IC2_NO_RET, FDOpenDir, fdopendir, int, fd)
/* Intercept chdir */
IC2_SIMPLE_1P(int, IC2_NO_RET, ChDir, chdir, const char *, dir)

#undef IC2_WAIT_ACK


static void
intercept_read (const int fd, const ssize_t ret)
{
  pthread_mutex_lock(&ic_fd_states_lock);
  try {
    fd_states->at(fd);
  } catch (std::exception& e) {
    fd_states->resize(fd + 1);
  }
  if ((*fd_states)[fd].read == false) {
    (*fd_states)[fd].read = true;
    pthread_mutex_unlock(&ic_fd_states_lock);
    int saved_errno = errno;
    msg::InterceptorMsg ic_msg;

    auto m = ic_msg.mutable_read();
    if (ret == -1) {
      m->set_error_no(saved_errno);
    }
    m->set_fd(fd);
    fb_send_msg(ic_msg, fb_sv_conn);

    errno = saved_errno;
  }
  pthread_mutex_unlock(&ic_fd_states_lock);
}


static void
intercept_write (const int fd, const ssize_t ret)
{
  pthread_mutex_lock(&ic_fd_states_lock);
  try {
    fd_states->at(fd);
  } catch (std::exception& e) {
    fd_states->resize(fd + 1);
  }
  if ((*fd_states)[fd].written == false) {
    (*fd_states)[fd].written = true;
    pthread_mutex_unlock(&ic_fd_states_lock);
    int saved_errno = errno;
    msg::InterceptorMsg ic_msg;

    auto m = ic_msg.mutable_write();
    if (ret == -1) {
      m->set_error_no(saved_errno);
    }
    m->set_fd(fd);
    fb_send_msg(ic_msg, fb_sv_conn);

    errno = saved_errno;
  }
  pthread_mutex_unlock(&ic_fd_states_lock);
}

static void
clear_file_state(const int fd) {
  if (fd >= 0) {
    pthread_mutex_lock(&ic_fd_states_lock);
    try {
      fd_states->at(fd);
    } catch (std::exception& e) {
      fd_states->resize(fd + 1);
    }
    (*fd_states)[fd].read = false;
    (*fd_states)[fd].written = false;
    pthread_mutex_unlock(&ic_fd_states_lock);
  }
}

static void
copy_file_state(const int to_fd, const int from_fd) {
  if ((to_fd >= 0) && (from_fd >= 0)) {
    pthread_mutex_lock(&ic_fd_states_lock);
    try {
      fd_states->at(to_fd);
    } catch (std::exception& e) {
      fd_states->resize(to_fd + 1);
    }
    (*fd_states)[to_fd] = (*fd_states)[from_fd];
    pthread_mutex_unlock(&ic_fd_states_lock);
  }
}


static void
intercept_exit (const int status)
{
  handle_exit(status, NULL);

  // exit handlers may call intercepted functions
  intercept_on = false;
  insert_end_marker();
}

/* make intercepted functions visible */
static pid_t
intercept_fork (const pid_t ret)
{
  msg::InterceptorMsg ic_msg;
  pid_t pid;

  if (ret == 0) {
    // child
    reset_fn_infos();
    ic_pid = pid = ic_orig_getpid();
    // unlock global interceptor lock if it is locked
    pthread_mutex_trylock(&ic_global_lock);
    pthread_mutex_unlock(&ic_global_lock);
    // reconnect to supervisor
    ic_orig_close(fb_sv_conn);
    fb_sv_conn = -1;
    init_supervisor_conn();
    auto m = ic_msg.mutable_fork_child();
    m->set_pid(pid);
    m->set_ppid(ic_orig_getppid());
    fb_send_msg(ic_msg, fb_sv_conn);
  } else {
    // parent
    auto m = ic_msg.mutable_fork_parent();
    m->set_pid(ic_pid);
    m->set_child_pid(ret);
    fb_send_msg(ic_msg, fb_sv_conn);
  }
  return ret;
}

/**
 * Wrapper for main function
 * The point of having it is the existence of main() function in programs
 * in which such programs exit from main() using return instead of
 * exit().
 * Exiting main() with return does not trigger exit handlers installed using
 * on_exit() thus makes FireBuild unable to gather exit status.
 * This wrapper sends the return value of main to the supervisor if
 * it has not been sent already. Most of the time this part of the code will not
 * run thanks to programs exit()-ing from main.
 */
extern int firebuild_fake_main(int argc, char **argv, char **env)
{
  auto orig_main = (int (*) (int, char**, char**))(argv[0]);
  auto orig_argv = (char**)(argv[1]);
  auto ret = orig_main(argc, orig_argv, env);
  handle_exit(ret, NULL);
  return ret;
}

/* make intercepted functions visible */
#pragma GCC visibility push(default)

#ifdef  __cplusplus
extern "C" {
#endif

#include "ic_file_ops.h"

#ifdef  __cplusplus
}
#endif

#pragma GCC visibility pop

} //namespace firebuild
