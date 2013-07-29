/**
 * This file is included several ways with different definitions for the
 * IC() macro.
 */

/**
 * Intercept open variants with varible length arg list.
 * mode is filled based on presence of O_CREAT flag
 */
#define IC_OPEN_VA(ret_type, name, parameters, body)			\
  IC(ret_type, name, parameters,					\
     {									\
       mode_t mode = 0;							\
       if (__oflag & O_CREAT) {						\
	 va_list ap;							\
	 va_start(ap, __oflag);						\
	 mode = va_arg(ap, mode_t);					\
	 va_end(ap);							\
       }								\
									\
       body;								\
       intercept_open(__file, __oflag, mode, ret);			\
     })


IC_OPEN_VA(int, open, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, open64, (__const char *__file, int __oflag, ...),
	   {ret = orig_fn(__file, __oflag, mode);})

IC_OPEN_VA(int, openat, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

IC_OPEN_VA(int, openat64, (int __fd, __const char *__file, int __oflag, ...),
	   {ret = orig_fn(__fd, __file, __oflag, mode);})

#define IC_CREATE(name)						\
  IC(int, name, (__const char *__file, __mode_t __mode), {	\
      ret = orig_fn(__file, __mode);				\
      intercept_creat(__file, __mode, ret);			\
    })

IC_CREATE(creat)
IC_CREATE(creat64)

/*  covered in unistd.h: lockf lockf64 */

/* unistd.h */

IC(int, close, (int __fd), {
      ret = orig_fn(__fd);
      intercept_close(__fd, ret);
    })

IC(int, access, (__const char *__name, int __type),
   {ret = orig_fn(__name, __type); intercept_access(__name, __type, ret);})
IC(int, euidaccess, (__const char *__name, int __type),
   {ret = orig_fn(__name, __type); intercept_eaccess(__name, __type, ret);})
IC(int, eaccess, (__const char *__name, int __type),
   {ret = orig_fn(__name, __type); intercept_eaccess(__name, __type, ret);})
IC(int, faccessat, (int __fd, __const char *__file, int __type, int __flag),
   {ret = orig_fn(__fd, __file, __type, __flag); intercept_faccessat(__fd, __file, __type, __flag, ret);})

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
IC(int, pipe, (int __pipedes[2]),
   {ret = orig_fn(__pipedes); intercept_pipe2(__pipedes, 0, ret);})
IC(int, pipe2, (int __pipedes[2], int __flags),
   {ret = orig_fn(__pipedes, __flags); intercept_pipe2(__pipedes, __flags, ret);})

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
IC(int, chown, (__const char *__file, __uid_t __owner, __gid_t __group), {
    ret = orig_fn(__file, __owner, __group);
    intercept_chown(__file, __owner, __group, ret);
  })

IC(int, fchown, (int __fd, __uid_t __owner, __gid_t __group), {
    ret = orig_fn(__fd, __owner, __group);
    intercept_fchown(__fd, __owner, __group, ret);
  })
IC(int, lchown, (__const char *__file, __uid_t __owner, __gid_t __group), {
    ret = orig_fn(__file, __owner, __group);
    intercept_lchown(__file, __owner, __group, ret);
  })
IC(int, fchownat, (int __fd, __const char *__file, __uid_t __owner,
		   __gid_t __group, int __flag), {
     ret = orig_fn(__fd, __file, __owner, __group, __flag);
     intercept_fchownat(__fd, __file, __owner, __group, __flag, ret);
   })

IC(int, chdir, (__const char *__path), {
    ret = orig_fn(__path);
    intercept_chdir(__path, ret);
  })

IC(int, fchdir, (int __fd), {
    ret = orig_fn(__fd);
    intercept_fchdir(__fd, ret);
  })

IC(char*, getcwd, (char *__buf, size_t __size), {
    ret = orig_fn(__buf, __size);
    intercept_getcwd(ret);
  })

IC(char*, get_current_dir_name, (void), {
    ret = orig_fn();
    intercept_getcwd(ret);
  })

IC(char*, getwd, (char *__buf), {
    ret = orig_fn(__buf);
    intercept_getcwd(ret);
  })

IC(int, dup, (int __fd),
   {ret = orig_fn(__fd); intercept_dup(__fd, ret);})
IC(int, dup2, (int __fd, int __fd2),
   {ret = orig_fn(__fd, __fd2); intercept_dup3(__fd, __fd2, 0, ret);})
IC(int, dup3, (int __fd, int __fd2, int __flags),
   {ret = orig_fn(__fd, __fd2, __flags); intercept_dup3(__fd, __fd2, __flags, ret);})

IC(int, execve, (__const char *__path, char *__const __argv[], char *__const __envp[]), {
    intercept_execve(false, __path, -1, __argv, __envp);
    ret = orig_fn(__path, __argv, __envp);
    intercept_execvfailed(ret);
  })
IC(int, fexecve, (int __fd, char *__const __argv[], char *__const __envp[]), {
    intercept_execve(false, NULL, __fd, __argv, environ);
    ret = orig_fn(__fd, __argv, __envp);
    intercept_execvfailed(ret);
  })
IC(int, execv, (__const char *__path, char *__const __argv[]), {
    intercept_execve(false, __path, -1, __argv, environ);
    ret = orig_fn(__path, __argv);
    intercept_execvfailed(ret);
  })

IC(int, execvp, (__const char *__file, char *__const __argv[]), {
    intercept_execve(true, __file, -1, __argv, environ);
    ret = orig_fn(__file, __argv);
    intercept_execvfailed(ret);
  })

IC(int, execvpe, (__const char *__file, char *__const __argv[],
		  char *__const __envp[]), {
     intercept_execve(true, __file, -1, __argv, __envp);
     ret = orig_fn(__file, __argv, __envp);
     intercept_execvfailed(ret);
   })

/* ignore: nice */

IC_VOID(void, exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);
  })

IC_VOID(void, _exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);
  })
IC_VOID(void, quick_exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);
  })

IC_GENERIC(long int, pathconf, (__const char *__path, int __name),
           {ret = orig_fn(__path, __name);})
IC_GENERIC(long int, fpathconf, (int __fd, int __name),
           {ret = orig_fn(__fd, __name);})
IC(long int, sysconf, (int __name),
   {ret = orig_fn(__name);intercept_sysconf(__name, ret);})
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

IC(__pid_t, fork, (void),
   {
     ret = orig_fn();
     intercept_fork(ret);
   })

/*  probably never used */
IC_GENERIC(char*, ttyname, (int __fd),
           {ret = orig_fn(__fd);})
IC_GENERIC(int, ttyname_r, (int __fd, char *__buf, size_t __buflen),
           {ret = orig_fn(__fd, __buf, __buflen);})

/* ignore: isatty ttyslot */

/* TODO !!! */
IC(int, link, (__const char *__from, __const char *__to),
   {ret = orig_fn(__from, __to); intercept_link(__from, __to, ret);})
IC(int, linkat, (int __fromfd, __const char *__from, int __tofd,
		 __const char *__to, int __flags),
   {
     ret = orig_fn(__fromfd, __from, __tofd, __to, __flags);
     intercept_linkat(__fromfd, __from, __tofd, __to, __flags, ret);
   })
IC(int, symlink, (__const char *__from, __const char *__to),
   {ret = orig_fn(__from, __to); intercept_symlink(__from, __to, ret);})
IC(ssize_t, readlink, (__const char *__restrict __path,
		       char *__restrict __buf, size_t __len), {
     ret = orig_fn(__path, __buf, __len);
     intercept_readlink_helper(-1, __path, __buf, __len, ret);
   })
IC(int, symlinkat, (__const char *__from, int __tofd, __const char *__to),
   {ret = orig_fn(__from, __tofd, __to);
     intercept_symlinkat( __from, __tofd, __to, ret);
   })
IC(ssize_t, readlinkat, (int __dirfd, __const char *__restrict __path,
			 char *__restrict __buf, size_t __len), {
     ret = orig_fn(__dirfd, __path, __buf, __len);
     intercept_readlink_helper(__dirfd, __path, __buf, __len, ret);
   })
IC(int, unlink, (__const char *__name), {
    ret = orig_fn(__name);
    intercept_unlink(__name, ret);
  })
IC(int, unlinkat, (int __fd, __const char *__name, int __flag),
   {ret = orig_fn(__fd, __name, __flag);
     intercept_unlinkat(__fd, __name, __flag, ret);
   })
IC(int, rmdir, (__const char *__path), {
    ret = orig_fn(__path);
    intercept_rmdir(__path, ret);
  })


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
IC(int, gethostname, (char *__name, size_t __len), {
    ret = orig_fn(__name, __len);
    intercept_gethostname(__name, __len, ret);
  })
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

// ignore: sync, getpagesize (calloc calls it)

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
/* we probably won't use offset in supervisor's logic */
IC(int, lockf, (int __fd, int __cmd, __off_t __len), {
    ret = orig_fn(__fd, __cmd, __len);
    intercept_lockf(__fd, __cmd, /* __len,*/ ret);
  })
IC(int, lockf64, (int __fd, int __cmd, __off64_t __len), {
    ret = orig_fn(__fd, __cmd, __len);
    intercept_lockf(__fd, __cmd, /*__len,*/ ret);
  })

/* ignored: fdatasync crypt encrypt swab */

IC_GENERIC(char*, ctermid, (char *__s),
           {ret = orig_fn(__s);})


// TODO stat.h
