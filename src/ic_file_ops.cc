/* from fcntl.h */

#include <fcntl.h>
#include <cstdarg>
#include <cassert>
#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <sys/time.h>
#include <sys/resource.h>

#include "intercept.h"
#include "fb-messages.pb.h"

using namespace std;

// TODO? 
//int fcntl (int __fd, int __cmd, ...);


typedef char* CHARS;

#define IC2_MSG_IC2_WITH_RET  m->set_ret(ret)
#define IC2_MSG_IC2_NO_RET

#define IC2_ERR_VAL_int -1
#define IC2_ERR_VAL_long -1
#define IC2_ERR_VAL_CHARS NULL

#define IC2_SIMPLE_NP(ics_rettype, ics_with_rettype,  ics_pmtype,	\
		      ics_pmname, ics_pars, ics_body)			\
  static void								\
  intercept_##ics_pmname ics_pars					\
  {									\
    InterceptorMsg ic_msg;						\
    ics_pmtype *m;							\
    int saved_errno = errno;						\
									\
    m = ic_msg.mutable_##ics_pmname();					\
    ics_body;								\
    IC2_MSG_##ics_with_rettype;						\
    if (ret == IC2_ERR_VAL_##ics_rettype) {				\
      m->set_error_no(saved_errno);					\
    }									\
    fb_send_msg(ic_msg, fb_sv_conn);					\
    errno = saved_errno;						\
  }

#define IC2_SIMPLE_0P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname)					\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		(ics_rettype ret), {})

#define IC2_SIMPLE_1P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname,					\
		      ics_ptype1, ics_pmattrname1)			\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		(ics_ptype1 ics_p1, ics_rettype ret),			\
		{m->set_##ics_pmattrname1(ics_p1);})

#define IC2_SIMPLE_2P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname,					\
		      ics_ptype1, ics_pmattrname1,			\
		      ics_ptype2, ics_pmattrname2)			\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		    (ics_ptype1 ics_p1, ics_ptype2 ics_p2,		\
		     ics_rettype ret),					\
		    {							\
		      m->set_##ics_pmattrname1(ics_p1);			\
		      m->set_##ics_pmattrname2(ics_p2);			\
		    })

#define IC2_SIMPLE_3P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname,					\
		      ics_ptype1, ics_pmattrname1,			\
		      ics_ptype2, ics_pmattrname2,			\
		      ics_ptype3, ics_pmattrname3)			\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		    (ics_ptype1 ics_p1, ics_ptype2 ics_p2,		\
		     ics_ptype3 ics_p3, ics_rettype ret),		\
		    {							\
		      m->set_##ics_pmattrname1(ics_p1);			\
		      m->set_##ics_pmattrname2(ics_p2);			\
		      m->set_##ics_pmattrname3(ics_p3);			\
		    })

#define IC2_SIMPLE_4P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname,					\
		      ics_ptype1, ics_pmattrname1,			\
		      ics_ptype2, ics_pmattrname2,			\
		      ics_ptype3, ics_pmattrname3,			\
		      ics_ptype4, ics_pmattrname4)			\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		    (ics_ptype1 ics_p1, ics_ptype2 ics_p2,		\
		     ics_ptype3 ics_p3, ics_ptype4 ics_p4,		\
		     ics_rettype ret),					\
		    {							\
		      m->set_##ics_pmattrname1(ics_p1);			\
		      m->set_##ics_pmattrname2(ics_p2);			\
		      m->set_##ics_pmattrname3(ics_p3);			\
		      m->set_##ics_pmattrname4(ics_p4);			\
		    })

#define IC2_SIMPLE_5P(ics_rettype, ics_with_rettype, ics_pmtype,	\
		      ics_pmname,					\
		      ics_ptype1, ics_pmattrname1,			\
		      ics_ptype2, ics_pmattrname2,			\
		      ics_ptype3, ics_pmattrname3,			\
		      ics_ptype4, ics_pmattrname4,			\
		      ics_ptype5, ics_pmattrname5)			\
  IC2_SIMPLE_NP(ics_rettype, ics_with_rettype, ics_pmtype, ics_pmname,	\
		    (ics_ptype1 ics_p1, ics_ptype2 ics_p2,		\
		     ics_ptype3 ics_p3, ics_ptype4 ics_p4,		\
		     ics_ptype5 ics_p5, ics_rettype ret),		\
		    {							\
		      m->set_##ics_pmattrname1(ics_p1);			\
		      m->set_##ics_pmattrname2(ics_p2);			\
		      m->set_##ics_pmattrname3(ics_p3);			\
		      m->set_##ics_pmattrname4(ics_p4);			\
		      m->set_##ics_pmattrname5(ics_p5);			\
		    })

/* Intercept unlink */
IC2_SIMPLE_1P(int, IC2_NO_RET, UnLink, unlink, const char *, path)
/* Intercept unlinkat */
IC2_SIMPLE_3P(int, IC2_NO_RET, UnLinkAt, unlinkat, int, dirfd, const char *, pathname, int, flags)
/* Intercept chdir */
IC2_SIMPLE_1P(int, IC2_NO_RET, ChDir, chdir, const char *, dir)
/* Intercept fchdir */
IC2_SIMPLE_1P(int, IC2_NO_RET, FChDir, fchdir, const int, dir)
/* Intercept close */
IC2_SIMPLE_1P(int, IC2_NO_RET, Close, close, const int, fd)
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
/* Intercept open variants */
IC2_SIMPLE_3P(int, IC2_WITH_RET, Open, open, const char *, file, const int, flags, const int, mode)
/* Intercept creat */
IC2_SIMPLE_2P(int, IC2_WITH_RET, Creat, creat, const char *, file, int, mode)
/* Intercept getcwd */
IC2_SIMPLE_0P(CHARS, IC2_WITH_RET, GetCwd, getcwd)
/* Intercept sysconf */
IC2_SIMPLE_1P(long, IC2_WITH_RET, Sysconf, sysconf, int, name)
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
intercept_readlink_helper(int fd, const char *path, const char *buf,
			  size_t len, ssize_t ret)
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

/* Intercept access */
IC2_SIMPLE_2P(int, IC2_NO_RET, Access, access, const char *, pathname, int, mode)
/* Intercept eaccess */
IC2_SIMPLE_2P(int, IC2_NO_RET, EAccess, eaccess, const char *, pathname, int, mode)
/* Intercept faccessat */
IC2_SIMPLE_4P(int, IC2_NO_RET, FAccessAt, faccessat, int, dirfd, const char *, pathname, int, mode, int, flags)


/* Intercept pipe variants */
static void
intercept_pipe2 (int pipefd[2], int flags, int ret)
{
  InterceptorMsg ic_msg;
  Pipe2 *m;
  int saved_errno = errno;

  m = ic_msg.mutable_pipe2();
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
intercept_exit (const int status)
{
  InterceptorMsg ic_msg;
  SupervisorMsg sv_msg;
  Exit *m;
  struct rusage ru;

  m = ic_msg.mutable_exit();
  m->set_exit_status(status);
  getrusage(RUSAGE_SELF, &ru);
  m->set_utime_m(ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000);
  m->set_stime_m(ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000);
  fb_send_msg(ic_msg, fb_sv_conn);
  fb_recv_msg(sv_msg, fb_sv_conn);
  if (!sv_msg.ack()) {
    // something unexpected happened ...
    assert(0);
  }
  // exit handlers may call intercepted functions
  intercept_on = false;

}

/* make intercepted functions visible */
static __pid_t
intercept_fork (const __pid_t ret)
{
  InterceptorMsg ic_msg;
  __pid_t pid;

  if (ret == 0) {
    // child
    ForkChild *m;
    reset_fn_infos();
    ic_pid = pid = ic_orig_getpid();
    // unlock global interceptor lock if it is locked
    pthread_mutex_trylock(&ic_global_lock);
    pthread_mutex_unlock(&ic_global_lock);
    // reconnect to supervisor
    ic_orig_close(fb_sv_conn);
    fb_sv_conn = -1;
    init_supervisor_conn();
    m = ic_msg.mutable_fork_child();
    m->set_pid(pid);
    m->set_ppid(ic_orig_getppid());
    fb_send_msg(ic_msg, fb_sv_conn);
  } else {
    // parent
    ForkParent *m;
    m = ic_msg.mutable_fork_parent();
    m->set_pid(ic_pid);
    m->set_child_pid(ret);
    fb_send_msg(ic_msg, fb_sv_conn);
  }
  return ret;
}

/* make intercepted functions visible */
#pragma GCC visibility push(default)

#include "ic_file_ops.h"

#pragma GCC visibility pop
