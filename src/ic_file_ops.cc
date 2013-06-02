/* from fcntl.h */

#include <fcntl.h>
#include <cstdarg>
#include <cassert>
#include <errno.h>
#include <unistd.h>
#include <iostream>

#include "intercept.h"
#include "fb-messages.pb.h"

using namespace std;

// TODO? 
//int fcntl (int __fd, int __cmd, ...);

/* Intercept open variants */
static void
intercept_open (const char *file, const int flags, const int mode,
	       const int ret, const int error_no)
{
  OpenFile m;
  m.set_pid(getpid());
  m.set_file(file);
  m.set_flags(flags);
  m.set_mode(mode);
  m.set_ret(ret);
  m.set_error_no(error_no);

  cout << "intercept open!" << endl;
  // TODO send to supervisor and collect file status if needed
}

/**
 * Intercept open variants with varible length arg list.
 * mode is filled based on presence of O_CREAT flag
 */
#define IC_OPEN_VA(ret_type, name, parameters, body)			\
  IC(ret_type, name, parameters,					\
     {									\
       int open_errno;							\
       mode_t mode = 0;							\
       if (__oflag & O_CREAT) {						\
	 va_list ap;							\
	 va_start(ap, __oflag);						\
	 mode = va_arg(ap, mode_t);					\
	 va_end(ap);							\
       }								\
									\
       body;								\
       open_errno = errno;						\
       intercept_open(__file, __oflag, mode, ret, open_errno);		\
       errno = open_errno;						\
     })


IC_OPEN_VA(int, open, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, open64, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, openat, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

IC_OPEN_VA(int, openat64, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

/* Intercept creat variants */
static void
intercept_create (const char *file, const int mode,
	       const int ret, const int error_no)
{
  CreateFile m;
  m.set_pid(getpid());
  m.set_file(file);
  m.set_mode(mode);
  m.set_ret(ret);
  m.set_error_no(error_no);

  cout << "intercept create!" << endl;
  // TODO send to supervisor
}


#define IC_CREATE(name)							\
  IC(int, name, (__const char *__file, __mode_t __mode), {		\
      int error_no;							\
      ret = orig_fn(__file, __mode);					\
      error_no = errno;							\
      intercept_create(__file, __mode, ret, error_no);			\
      errno = error_no;							\
    })

IC_CREATE(creat)
IC_CREATE(creat64)

/*  covered in unistd.h: lockf lockf64 */

/* unistd.h */

/* Intercept close */
static void
intercept_close (const int fd, const int ret)
{
  CloseFile m;
  m.set_pid(getpid());
  m.set_fd(fd);
  m.set_ret(ret);

  cout << "intercept close!" << endl;
  // TODO send to supervisor
}

IC(int, close, (int __fd), {
      ret = orig_fn(__fd);
      intercept_close(__fd, ret);
    })

IC_GENERIC(int, access, (__const char *__name, int __type),
	   {ret = orig_fn(__name, __type);})
IC_GENERIC(int, euidaccess, (__const char *__name, int __type),
	   {ret = orig_fn(__name, __type);})
IC_GENERIC(int, eaccess, (__const char *__name, int __type),
	   {ret = orig_fn(__name, __type);})
IC_GENERIC(int, faccessat, (int __fd, __const char *__file, int __type, int __flag),
	   {ret = orig_fn(__fd, __file, __type, __flag);})

// ignored: lseek lseek64
// those don't let new information enter the process

// TODO finish to handle stdio
IC_GENERIC(ssize_t, read, (int __fd, void *__buf, size_t __nbytes),
	   {ret = orig_fn(__fd, __buf, __nbytes);})
IC_GENERIC(ssize_t, write, (int __fd, __const void *__buf, size_t __n),
	   {ret = orig_fn(__fd, __buf, __n);})
IC_GENERIC(ssize_t, pread, (int __fd, void *__buf, size_t __nbytes, __off_t __offset),
	   {ret = orig_fn(__fd, __buf, __nbytes, __offset);})
IC_GENERIC(ssize_t, pwrite, (int __fd, __const void *__buf, size_t __n, __off_t __offset),
	   {ret = orig_fn(__fd, __buf, __n, __offset);})
IC_GENERIC(ssize_t, pread64, (int __fd, void *__buf, size_t __nbytes, __off_t __offset),
	   {ret = orig_fn(__fd, __buf, __nbytes, __offset);})
IC_GENERIC(ssize_t, pwrite64, (int __fd, __const void *__buf, size_t __n, __off_t __offset),
	   {ret = orig_fn(__fd, __buf, __n, __offset);})
// TODO intercept to handle communication between forked children and parent
IC_GENERIC(int, pipe, (int __pipedes[2]),
	   {ret = orig_fn(__pipedes);})
IC_GENERIC(int, pipe2, (int __pipedes[2], int __flags),
	   {ret = orig_fn(__pipedes, __flags);})

// TODO those may affect output if the process measures time that way
// usually the calls can be ignored
IC_GENERIC(unsigned int, alarm, (unsigned int __seconds),
	   {ret = orig_fn(__seconds);})
IC_GENERIC(unsigned int, sleep, (unsigned int __seconds),
	   {ret = orig_fn(__seconds);})
IC_GENERIC(__useconds_t, ualarm, (__useconds_t __value, __useconds_t __interval),
	   {ret = orig_fn(__value, __interval);})
IC_GENERIC(int, usleep, (__useconds_t __useconds),
	   {ret = orig_fn(__useconds);})
IC_GENERIC(int, pause, (void),
	   {ret = orig_fn();})

// TODO finish
IC_GENERIC(int, chown, (__const char *__file, __uid_t __owner, __gid_t __group),
	   {ret = orig_fn(__file, __owner, __group);})
IC_GENERIC(int, fchown, (int __fd, __uid_t __owner, __gid_t __group),
	   {ret = orig_fn(__fd, __owner, __group);})
IC_GENERIC(int, lchown, (__const char *__file, __uid_t __owner, __gid_t __group),
	   {ret = orig_fn(__file, __owner, __group);})
IC_GENERIC(int, fchownat, (int __fd, __const char *__file, __uid_t __owner,__gid_t __group, int __flag),
	   {ret = orig_fn(__fd, __file, __owner, __group, __flag);})
IC_GENERIC(int, chdir, (__const char *__path),
	   {ret = orig_fn(__path);})

IC_GENERIC(int, fchdir, (int __fd),
           {ret = orig_fn(__fd);})
IC_GENERIC(char*, getcwd, (char *__buf, size_t __size),
           {ret = orig_fn(__buf, __size);})
IC_GENERIC(char*, get_current_dir_name, (void),
           {ret = orig_fn();})
IC_GENERIC(char*, getwd, (char *__buf),
           {ret = orig_fn(__buf);})
IC_GENERIC(int, dup, (int __fd),
           {ret = orig_fn(__fd);})
IC_GENERIC(int, dup2, (int __fd, int __fd2),
           {ret = orig_fn(__fd, __fd2);})
IC_GENERIC(int, dup3, (int __fd, int __fd2, int __flags),
	   {ret = orig_fn(__fd, __fd2, __flags);})

IC_GENERIC(int, execve, (__const char *__path, char *__const __argv[], char *__const __envp[]),
           {ret = orig_fn(__path, __argv, __envp);})
IC_GENERIC(int, fexecve, (int __fd, char *__const __argv[], char *__const __envp[]),
           {ret = orig_fn(__fd, __argv, __envp);})
IC_GENERIC(int, execv, (__const char *__path, char *__const __argv[]),
           {ret = orig_fn(__path, __argv);})

/* TODO why on earth do we need those fns? */
IC_GENERIC(int, execle, (__const char *__path, __const char *__arg, ...),
           {/* to not forget about that */
	     assert( 0 && "processes calling execle are not supported");
	     ret = 0;
	   })

IC_GENERIC(int, execl, (__const char *__path, __const char *__arg, ...),
           {/* to not forget about that */
	     assert( 0 && "processes calling execle are not supported");
	     ret = 0;
	   })

IC_GENERIC(int, execvp, (__const char *__file, char *__const __argv[]),
           {ret = orig_fn(__file, __argv);})

/* TODO */
IC_GENERIC(int, execlp, (__const char *__file, __const char *__arg, ...),
           {/* to not forget about that */
	     assert( 0 && "processes calling execle are not supported");
	     ret = 0;
	   })

IC_GENERIC(int, execvpe, (__const char *__file, char *__const __argv[],
			  char *__const __envp[]),
           {ret = orig_fn(__file, __argv, __envp);})

/* ignore: nice */


/* TODO finish */
static void
intercept_exit (const int status)
{
  GenericCall m;
  m.set_call("exit");

  cout << "intercept exit!" << endl;
  // TODO send to supervisor
}

/* TODO */
IC_VOID(void, _exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);
  })

IC_GENERIC(long int, pathconf, (__const char *__path, int __name),
           {ret = orig_fn(__path, __name);})
IC_GENERIC(long int, fpathconf, (int __fd, int __name),
           {ret = orig_fn(__fd, __name);})
IC_GENERIC(long int, sysconf, (int __name),
           {ret = orig_fn(__name);})
IC_GENERIC(size_t, confstr, (int __name, char *__buf, size_t __len),
           {ret = orig_fn(__name, __buf, __len);})
IC_GENERIC(__pid_t, getpid, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, getppid, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, getpgrp, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, __getpgid, (__pid_t __pid),
           {ret = orig_fn(__pid);})
IC_GENERIC(__pid_t, getpgid, (__pid_t __pid),
           {ret = orig_fn(__pid);})
IC_GENERIC(int, setpgid, (__pid_t __pid, __pid_t __pgid),
           {ret = orig_fn(__pid, __pgid);})
IC_GENERIC(int, setpgrp, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, setsid, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, getsid, (__pid_t __pid),
           {ret = orig_fn(__pid);})
IC_GENERIC(__uid_t, getuid, (void),
           {ret = orig_fn();})
IC_GENERIC(__uid_t, geteuid, (void),
           {ret = orig_fn();})
IC_GENERIC(__gid_t, getgid, (void),
           {ret = orig_fn();})
IC_GENERIC(__gid_t, getegid, (void),
           {ret = orig_fn();})
IC_GENERIC(int, getgroups, (int __size, __gid_t __list[]),
           {ret = orig_fn(__size, __list);})
IC_GENERIC(int, group_member, (__gid_t __gid),
           {ret = orig_fn(__gid);})
IC_GENERIC(int, setuid, (__uid_t __uid),
           {ret = orig_fn(__uid);})
IC_GENERIC(int, setreuid, (__uid_t __ruid, __uid_t __euid),
           {ret = orig_fn(__ruid, __euid);})
IC_GENERIC(int, seteuid, (__uid_t __uid),
           {ret = orig_fn(__uid);})
IC_GENERIC(int, setgid, (__gid_t __gid),
           {ret = orig_fn(__gid);})
IC_GENERIC(int, setregid, (__gid_t __rgid, __gid_t __egid),
           {ret = orig_fn(__rgid, __egid);})
IC_GENERIC(int, setegid, (__gid_t __gid),
           {ret = orig_fn(__gid);})
IC_GENERIC(int, getresuid, (__uid_t *__ruid, __uid_t *__euid, __uid_t *__suid),
           {ret = orig_fn(__ruid, __euid, __suid);})
IC_GENERIC(int, getresgid, (__gid_t *__rgid, __gid_t *__egid, __gid_t *__sgid),
           {ret = orig_fn(__rgid, __egid, __sgid);})
IC_GENERIC(int, setresuid, (__uid_t __ruid, __uid_t __euid, __uid_t __suid),
           {ret = orig_fn(__ruid, __euid, __suid);})
IC_GENERIC(int, setresgid, (__gid_t __rgid, __gid_t __egid, __gid_t __sgid),
           {ret = orig_fn(__rgid, __egid, __sgid);})

/* TODO */
/*
IC_GENERIC(__pid_t, fork, (void),
           {ret = orig_fn();})
IC_GENERIC(__pid_t, vfork, (void),
           {ret = orig_fn();})
*/
/*  probably never used */
IC_GENERIC(char*, ttyname, (int __fd),
           {ret = orig_fn(__fd);})
IC_GENERIC(int, ttyname_r, (int __fd, char *__buf, size_t __buflen),
           {ret = orig_fn(__fd, __buf, __buflen);})

/* ignore: isatty ttyslot */

/* TODO !!! */
IC_GENERIC(int, link, (__const char *__from, __const char *__to),
           {ret = orig_fn(__from, __to);})
IC_GENERIC(int, linkat, (int __fromfd, __const char *__from, int __tofd,
			 __const char *__to, int __flags),
           {ret = orig_fn(__fromfd, __from, __tofd, __to, __flags);})
IC_GENERIC(int, symlink, (__const char *__from, __const char *__to),
           {ret = orig_fn(__from, __to);})
IC_GENERIC(ssize_t, readlink, (__const char *__restrict __path,
			       char *__restrict __buf, size_t __len),
           {ret = orig_fn(__path, __buf, __len);})
IC_GENERIC(int, symlinkat, (__const char *__from, int __tofd, __const char *__to),
           {ret = orig_fn(__from, __tofd, __to);})
IC_GENERIC(ssize_t, readlinkat, (int __fd, __const char *__restrict __path,
				 char *__restrict __buf, size_t __len),
           {ret = orig_fn(__fd, __path, __buf, __len);})
IC_GENERIC(int, unlink, (__const char *__name),
           {ret = orig_fn(__name);})
IC_GENERIC(int, unlinkat, (int __fd, __const char *__name, int __flag),
           {ret = orig_fn(__fd, __name, __flag);})
IC_GENERIC(int, rmdir, (__const char *__path),
           {ret = orig_fn(__path);})


IC_GENERIC(__pid_t, tcgetpgrp, (int __fd),
           {ret = orig_fn(__fd);})
IC_GENERIC(int, tcsetpgrp, (int __fd, __pid_t __pgrp_id),
           {ret = orig_fn(__fd, __pgrp_id);})
IC_GENERIC(char*, getlogin, (void),
           {ret = orig_fn();})
IC_GENERIC(int, getlogin_r, (char *__name, size_t __name_len),
           {ret = orig_fn(__name, __name_len);})
IC_GENERIC(int, setlogin, (__const char *__name),
           {ret = orig_fn(__name);})
IC_GENERIC(int, gethostname, (char *__name, size_t __len),
           {ret = orig_fn(__name, __len);})
IC_GENERIC(int, sethostname, (__const char *__name, size_t __len),
           {ret = orig_fn(__name, __len);})
IC_GENERIC(int, sethostid, (long int __id),
           {ret = orig_fn(__id);})
IC_GENERIC(int, getdomainname, (char *__name, size_t __len),
           {ret = orig_fn(__name, __len);})
IC_GENERIC(int, setdomainname, (__const char *__name, size_t __len),
           {ret = orig_fn(__name, __len);})

/* ignore: vhangup revoke profil acct */

IC_GENERIC(char*, getusershell, (void),
           {ret = orig_fn();})
IC_GENERIC_VOID(void, endusershell, (void),
           {orig_fn();})
IC_GENERIC_VOID(void, setusershell, (void),
           {orig_fn();})

IC_GENERIC(int, daemon, (int __nochdir, int __noclose),
           {ret = orig_fn(__nochdir, __noclose);})
IC_GENERIC(int, chroot, (__const char *__path),
           {ret = orig_fn(__path);})
/* this may be ignored */
IC_GENERIC(char*, getpass, (__const char *__prompt),
           {ret = orig_fn(__prompt);})

// ignore fsync

IC_GENERIC(long int, gethostid, (void),
           {ret = orig_fn();})

/* ignore: sync */

IC_GENERIC(int, getpagesize, (void),
           {ret = orig_fn();})
IC_GENERIC(int, getdtablesize, (void),
           {ret = orig_fn();})
IC_GENERIC(int, truncate, (__const char *__file, __off_t __length),
           {ret = orig_fn(__file, __length);})
IC_GENERIC(int, truncate64, (__const char *__file, __off64_t __length),
           {ret = orig_fn(__file, __length);})
IC_GENERIC(int, ftruncate, (int __fd, __off_t __length),
           {ret = orig_fn(__fd, __length);})
IC_GENERIC(int, ftruncate64, (int __fd, __off64_t __length),
           {ret = orig_fn(__fd, __length);})

/* ignore: brk sbrk */

/* TODO test */
IC_GENERIC(long int, syscall, (long int __sysno, __gnuc_va_list __ap),
           {ret = orig_fn(__sysno, __ap);})
IC_GENERIC(int, lockf, (int __fd, int __cmd, __off_t __len),
           {ret = orig_fn(__fd, __cmd, __len);})
IC_GENERIC(int, lockf64, (int __fd, int __cmd, __off64_t __len),
           {ret = orig_fn(__fd, __cmd, __len);})

/* ignored: fdatasync crypt encrypt swab */

IC_GENERIC(char*, ctermid, (char *__s),
           {ret = orig_fn(__s);})


