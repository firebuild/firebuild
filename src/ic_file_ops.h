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
       clear_file_state(ret);						\
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
      clear_file_state(ret);					\
    })

IC_CREATE(creat)
IC_CREATE(creat64)

/* libc internal */
IC(int, __libc_start_main, (int (*main) (int, char **, char **),
                              int argc, char **ubp_av,
                              void (*init) (void), void (*fini) (void),
                            void (*rtld_fini) (void), void (* stack_end)), {
     char * main_and_argv[2];
     main_and_argv[0] = (char *)main;
     main_and_argv[1] = (char *)ubp_av;
     intercept_on = false;
     insert_end_marker();
     ret = orig_fn(firebuild_fake_main, argc, main_and_argv, init, fini, rtld_fini, stack_end);
                                })

/*  covered in unistd.h: lockf lockf64 */

/* unistd.h */

IC(int, close, (int __fd), {
      ret = orig_fn(__fd);
      intercept_close(__fd, ret);
      clear_file_state(ret);
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
IC(ssize_t, read, (int __fd, void *__buf, size_t __nbytes),
   {ret = orig_fn(__fd, __buf, __nbytes); intercept_read(__fd, ret);})
IC(ssize_t, write, (int __fd, __const void *__buf, size_t __n),
   {ret = orig_fn(__fd, __buf, __n); intercept_write(__fd, ret);})
IC(ssize_t, pread, (int __fd, void *__buf, size_t __nbytes, __off_t __offset),
   {ret = orig_fn(__fd, __buf, __nbytes, __offset); intercept_read(__fd, ret);})
IC(ssize_t, pwrite, (int __fd, __const void *__buf, size_t __n, __off_t __offset),
   {ret = orig_fn(__fd, __buf, __n, __offset); intercept_write(__fd, ret);})
IC(ssize_t, pread64, (int __fd, void *__buf, size_t __nbytes, __off_t __offset),
   {ret = orig_fn(__fd, __buf, __nbytes, __offset); intercept_read(__fd, ret);})
IC(ssize_t, pwrite64, (int __fd, __const void *__buf, size_t __n, __off_t __offset),
   {ret = orig_fn(__fd, __buf, __n, __offset); intercept_write(__fd, ret);})
// TODO intercept to handle communication between forked children and parent
IC(int, pipe, (int __pipedes[2]), {
    ret = orig_fn(__pipedes);
    intercept_pipe2(__pipedes, 0, ret);
    clear_file_state(__pipedes[0]);
    clear_file_state(__pipedes[1]);
})
IC(int, pipe2, (int __pipedes[2], int __flags), {
    ret = orig_fn(__pipedes, __flags);
    intercept_pipe2(__pipedes, __flags, ret);
    clear_file_state(__pipedes[0]);
    clear_file_state(__pipedes[1]);
  })

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

IC(int, dup, (int __fd), {
    ret = orig_fn(__fd);
    intercept_dup(__fd, ret);
    copy_file_state(ret, __fd);
  })
IC(int, dup2, (int __fd, int __fd2), {
    ret = orig_fn(__fd, __fd2);
    intercept_dup3(__fd, __fd2, 0, ret);
    if (ret != -1) {
      copy_file_state (__fd2, __fd);
    }
  })
IC(int, dup3, (int __fd, int __fd2, int __flags), {
    ret = orig_fn(__fd, __fd2, __flags);
    intercept_dup3(__fd, __fd2, __flags, ret);
    if (ret != -1) {
      copy_file_state (__fd2, __fd);
    }
  })

IC(int, execve, (__const char *__path, char *__const __argv[], char *__const __envp[]), {
    intercept_execve(false, __path, -1, __argv, __envp);
    ret = orig_fn(__path, __argv, __envp);
    init_supervisor_conn();
    intercept_execvfailed(ic_pid, ret);
  })
IC(int, fexecve, (int __fd, char *__const __argv[], char *__const __envp[]), {
    intercept_execve(false, NULL, __fd, __argv, environ);
    ret = orig_fn(__fd, __argv, __envp);
    init_supervisor_conn();
    intercept_execvfailed(ic_pid, ret);
  })
IC(int, execv, (__const char *__path, char *__const __argv[]), {
    intercept_execve(false, __path, -1, __argv, environ);
    ret = orig_fn(__path, __argv);
    init_supervisor_conn();
    intercept_execvfailed(ic_pid, ret);
  })

IC(int, execvp, (__const char *__file, char *__const __argv[]), {
    intercept_execve(true, __file, -1, __argv, environ);
    ret = orig_fn(__file, __argv);
    init_supervisor_conn();
    intercept_execvfailed(ic_pid, ret);
  })

IC(int, execvpe, (__const char *__file, char *__const __argv[],
		  char *__const __envp[]), {
     intercept_execve(true, __file, -1, __argv, __envp);
     ret = orig_fn(__file, __argv, __envp);
     init_supervisor_conn();
     intercept_execvfailed(ic_pid, ret);
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
    while (true);})
IC_VOID(void, _Exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);})
IC_VOID(void, quick_exit, (int __status), {
    intercept_exit(__status);
    orig_fn(__status);
    while (true);
  })

IC(long int, pathconf, (__const char *__path, int __name),
   {ret = orig_fn(__path, __name); intercept_pathconf(__path, __name, ret);})
IC(long int, fpathconf, (int __fd, int __name),
   {ret = orig_fn(__fd, __name); intercept_fpathconf(__fd, __name, ret);})
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
IC(int, getdomainname, (char *__name, size_t __len), {
    ret = orig_fn(__name, __len);
    intercept_getdomainname(__name, __len, ret);
  })
IC_GENERIC(int, setdomainname, (__const char *__name, size_t __len),
           {ret = orig_fn(__name, __len);})

IC_GENERIC(int, vhangup, (void),
           {ret = orig_fn();})
IC_GENERIC(int, revoke, (const char *file),
           {ret = orig_fn(file);})
IC_GENERIC(int, profil, (unsigned short int *sample_buffer, size_t size,
                   size_t offset, unsigned int scale),
           {ret = orig_fn(sample_buffer, size, offset, scale);})
IC_GENERIC(int, acct, (const char *filename),
           {ret = orig_fn(filename);})

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
IC(int, truncate, (__const char *__file, __off_t __length), {
    ret = orig_fn(__file, __length);
    intercept_truncate(__file, __length, ret);
  })
IC(int, truncate64, (__const char *__file, __off64_t __length), {
    ret = orig_fn(__file, __length);
    intercept_truncate(__file, __length, ret);
  })
IC(int, ftruncate, (int __fd, __off_t __length), {
    ret = orig_fn(__fd, __length);
    intercept_ftruncate(__fd, __length, ret);
  })
IC(int, ftruncate64, (int __fd, __off64_t __length), {
    ret = orig_fn(__fd, __length);
    intercept_ftruncate(__fd, __length, ret);
  })

/* ignore: brk sbrk */

/* TODO
IC_GENERIC(long int, syscall, (long int __sysno, ...),
           {ret = orig_fn(__sysno, XXX);})
*/
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


// TODO intercept fns
IC_GENERIC(int, stat, (const char *file, struct stat *buf), {
    ret = orig_fn(file, buf); /*intercept_stat(file, buf, ret);*/})
IC_GENERIC(int, fstat, (int fd, struct stat *buf), {
    ret = orig_fn(fd, buf); /*intercept_fstat64(fd, buf, ret);*/})
IC_GENERIC(int, stat64, (const char *file, struct stat64 *buf), {
    ret = orig_fn(file, buf); /*intercept_stat64(file, buf, ret);*/})
IC_GENERIC(int, fstat64, (int fd, struct stat64 *buf), {
    ret = orig_fn(fd, buf); /*intercept_fstat64(fd, buf, ret);*/})
IC_GENERIC(int, fstatat, (int fd, const char * file,
			  struct stat *buf, int flag), {
	     ret = orig_fn(fd, file, buf, flag); /*intercept_();*/})
IC_GENERIC(int, fstatat64, (int fd, const char * file,
			    struct stat64 *buf, int flag), {
	     ret = orig_fn(fd, file, buf, flag); /*intercept_();*/})
IC_GENERIC(int, lstat, (const char *file, struct stat *buf), {
    ret = orig_fn(file, buf); /*intercept_lstat(file, buf, ret);*/})
IC_GENERIC(int, lstat64, (const char *file, struct stat64 *buf), {
    ret = orig_fn(file, buf); /*intercept_lstat64(file, buf, ret);*/})

IC_GENERIC(int, chmod, (const char *file, __mode_t mode), {
    ret = orig_fn(file, mode); /*intercept_();*/})
IC_GENERIC(int, lchmod, (const char *file, __mode_t mode), {
    ret = orig_fn(file, mode); /*intercept_();*/})
IC_GENERIC(int, fchmod, (int fd, __mode_t mode), {
    ret = orig_fn(fd, mode); /*intercept_();*/})
IC_GENERIC(int, fchmodat, (int fd, const char *file, __mode_t mode, int flag), {
    ret = orig_fn(fd, file, mode, flag); /*intercept_();*/})
IC_GENERIC(mode_t, umask, (__mode_t mask), {
    ret = orig_fn(mask); /*intercept_();*/})
IC_GENERIC(mode_t, getumask, (void), {
    ret = orig_fn(); /*intercept_();*/})
IC_GENERIC(int, mkdir, (const char *path, __mode_t mode), {
    ret = orig_fn(path, mode); /*intercept_();*/})
IC_GENERIC(int, mkdirat, (int fd, const char *path, __mode_t mode), {
    ret = orig_fn(fd, path, mode); /*intercept_();*/})
IC_GENERIC(int, mknod, (const char *path, __mode_t mode, __dev_t dev), {
    ret = orig_fn(path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, mknodat, (int fd, const char *path, __mode_t mode, __dev_t dev), {
    ret = orig_fn(fd, path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, mkfifo, (const char *path, __mode_t mode), {
    ret = orig_fn(path, mode); /*intercept_();*/})
IC_GENERIC(int, mkfifoat, (int fd, const char *path, __mode_t mode), {
    ret = orig_fn(fd, path, mode); /*intercept_();*/})
IC(int, utimensat, (int fd, const char *path, const struct timespec times[2],
		    int flags), {
     ret = orig_fn(fd, path, times, flags);
     intercept_utime(fd, path, (flags & AT_SYMLINK_NOFOLLOW)?true:false, ret);})
IC(int, futimens, (int fd, const struct timespec times[2]), {
    ret = orig_fn(fd, times); intercept_futime(fd, ret);})
IC_GENERIC(int, __fxstat, (int ver, int fildes, struct stat *stat_buf), {
    ret = orig_fn(ver, fildes, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __xstat, (int ver, const char *filename, struct stat *stat_buf), {
    ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __lxstat, (int ver, const char *filename, struct stat *stat_buf), {
    ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __fxstatat, (int ver, int fildes, const char *filename,
			     struct stat *stat_buf, int flag), {
	     ret = orig_fn(ver, fildes, filename, stat_buf, flag); /*intercept_();*/})
IC_GENERIC(int, __fxstat64, (int ver, int fildes, struct stat64 *stat_buf), {
    ret = orig_fn(ver, fildes, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __xstat64, (int ver, const char *filename, struct stat64 *stat_buf), {
    ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __lxstat64, (int ver, const char *filename, struct stat64 *stat_buf), {
    ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __fxstatat64, (int ver, int fildes, const char *filename,
			     struct stat64 *stat_buf, int flag), {
	     ret = orig_fn(ver, fildes, filename, stat_buf, flag); /*intercept_();*/})
IC_GENERIC(int, __xmknod, (int ver, const char *path, mode_t mode, __dev_t *dev), {
    ret = orig_fn(ver, path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, __xmknodat, (int ver, int fd, const char *path,
					__mode_t mode, __dev_t *dev), {
	     ret = orig_fn(ver, fd, path, mode, dev); /*intercept_();*/})

// TODO finish stdio.h
IC(int, remove, (const char *filename), {
    ret = orig_fn(filename); intercept_remove(filename, ret);})
IC(int, rename, (const char *oldpath, const char *newpath), {
    ret = orig_fn(oldpath, newpath); intercept_rename(oldpath, newpath, ret);})
IC(int, renameat, (int oldfd, const char *oldpath, int newfd, const char *newpath), {
    ret = orig_fn(oldfd, oldpath, newfd, newpath);
    intercept_renameat(oldfd, oldpath, newfd, newpath, ret);})

IC(FILE*, fopen, (const char *filename, const char *modes),{
    ret = orig_fn(filename, modes);
    intercept_fopen(filename, modes, (ret)?fileno(ret):(-1));})
IC(FILE*, fopen64, (const char *filename, const char *modes),{
    ret = orig_fn(filename, modes);
    intercept_fopen(filename, modes, (ret)?fileno(ret):(-1));})
IC(FILE*, freopen, (const char *filename, const char *modes, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(filename, modes, stream);
    intercept_freopen(filename, modes, stream_fileno,(ret)?fileno(ret):(-1));})
IC(FILE*, freopen64, (const char *filename, const char *modes, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(filename, modes, stream);
    intercept_freopen(filename, modes, stream_fileno,(ret)?fileno(ret):(-1));})

// ignore fdopen, since it does not open new file
IC(int, fclose, (FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_close(stream_fileno, (ret == EOF)?-1:ret);})
IC(int, fcloseall, (void),{
    ret = orig_fn();
    intercept_fcloseall((ret == EOF)?-1:ret);})


IC(void*, dlopen, (const char *filename, int flag), {
    ret = orig_fn(filename, flag); intercept_dlopen(filename, flag, ret);})

// dirent.h
IC(DIR *, opendir, (const char *name), {
    ret = orig_fn(name); intercept_opendir(name, ret);})
IC(DIR *, fdopendir, (int fd), {
    ret = orig_fn(fd); intercept_fdopendir(fd, ret);})
IC_GENERIC(int, closedir, (DIR *dirp),
           {ret = orig_fn(dirp);})
IC_GENERIC(struct dirent *, readdir, (DIR *dirp),
           {ret = orig_fn(dirp);})
IC_GENERIC(struct dirent64 *, readdir64, (DIR *dirp),
           {ret = orig_fn(dirp);})
IC_GENERIC(int, readdir_r, (DIR *dirp, struct dirent *entry,
			    struct dirent **result),
           {ret = orig_fn(dirp, entry, result);})
IC_GENERIC(int, readdir64_r, (DIR *dirp, struct dirent64 *entry,
			    struct dirent64 **result),
           {ret = orig_fn(dirp, entry, result);})
IC_GENERIC_VOID(void, rewinddir, (DIR *dirp),
		{orig_fn(dirp);})
IC_GENERIC_VOID(void, seekdir, (DIR *dirp, long int pos),
		{orig_fn(dirp, pos);})
IC_GENERIC(long int, telldir, (DIR *dirp),
           {ret = orig_fn(dirp);})
IC_GENERIC(int, dirfd, (DIR *dirp),
           {ret = orig_fn(dirp);})
// ignore scandir scandir64 alphasort
IC_GENERIC(ssize_t, getdirentries, (int fd, char *buf, size_t nbytes,
				    off_t *basep),
           {ret = orig_fn(fd, buf, nbytes, basep);})
IC_GENERIC(ssize_t, getdirentries64, (int fd, char *buf, size_t nbytes,
				      off64_t *basep),
           {ret = orig_fn(fd, buf, nbytes, basep);})
// ignore versionsort versionsort64


/** generate two intercepted functions, one for name and one for name_unlocked */
#define IC_WITH_UNLOCKED(ret_type, name, parameters, body)		\
  IC(ret_type, name, parameters, body)					\
  IC(ret_type, name##_unlocked, parameters, body)

IC_WITH_UNLOCKED(size_t, fread, (void *ptr, size_t size, size_t nmemb, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(ptr, size, nmemb, stream);
    intercept_read(stream_fileno, (ret < nmemb)?-1:ret);})
IC_WITH_UNLOCKED(size_t, fwrite, (const void *ptr, size_t size, size_t nmemb, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(ptr, size, nmemb, stream);
    intercept_write(stream_fileno, (ret < nmemb)?-1:ret);})

IC_WITH_UNLOCKED(int, fputc, (int c, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == EOF)?-1:ret);})
IC_WITH_UNLOCKED(wint_t, fputwc, (wchar_t c, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == WEOF)?-1:ret);})
IC_WITH_UNLOCKED(int, fputs, (const char *s, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(s, stream);
    intercept_write(stream_fileno, (ret == EOF)?-1:ret);})
IC_WITH_UNLOCKED(int, putc, (int c, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == EOF)?-1:ret);})
IC_WITH_UNLOCKED(wint_t, putwc, (wchar_t c, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == WEOF)?-1:ret);})
IC_WITH_UNLOCKED(int, putchar, (int c),{
    ret = orig_fn(c);
    intercept_write(STDOUT_FILENO, (ret == EOF)?-1:ret);})
IC_WITH_UNLOCKED(wint_t, putwchar, (wchar_t c),{
    ret = orig_fn(c);
    intercept_write(STDOUT_FILENO, (ret == WEOF)?-1:ret);})
IC(int, puts, (const char *s),{
    ret = orig_fn(s);
    intercept_write(STDOUT_FILENO, (ret == EOF)?-1:ret);})

IC_WITH_UNLOCKED(int, fgetc, (FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, fgetwc, (FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == WEOF)?-1:2);})
IC_WITH_UNLOCKED(char*, fgets, (char *s, int n, FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(s, n, stream);
    intercept_read(stream_fileno, ret?strlen(ret):-1);})
IC_WITH_UNLOCKED(int, getc, (FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, getwc, (FILE *stream),{
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == WEOF)?-1:2);})
IC_WITH_UNLOCKED(int, getchar, (void),{
    ret = orig_fn();
    intercept_read(STDOUT_FILENO, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, getwchar, (void),{
    ret = orig_fn();
    intercept_read(STDOUT_FILENO, (ret == WEOF)?-1:2);})
/* should be never used, see man gets*/
IC(char*, gets, (char *s),{
    ret = orig_fn(s);
    intercept_read(STDOUT_FILENO, ret?strlen(ret):-1);})


// socket.h
IC_GENERIC(int, socket, (int domain, int type, int protocol),
           {ret = orig_fn(domain, type, protocol);})
IC_GENERIC(int, socketpair, (int domain, int type, int protocol,int sv[2]),
           {ret = orig_fn(domain, type, protocol, sv);})
IC_GENERIC(int, bind, (int fd, const struct sockaddr *addr, socklen_t len),
           {ret = orig_fn(fd, addr, len);})
IC_GENERIC(int, getsockname, (int fd, struct sockaddr *addr, socklen_t *addrlen),
	   {ret = orig_fn(fd, addr, addrlen);})
IC_GENERIC(int, connect, (int fd, const struct sockaddr *addr, socklen_t len),
           {ret = orig_fn(fd, addr, len);})
IC_GENERIC(int, getpeername, (int fd, struct sockaddr *addr, socklen_t *addrlen),
	   {ret = orig_fn(fd, addr, addrlen);})
IC_GENERIC(ssize_t, send, (int fd, const void *buf, size_t n, int flags),
           {ret = orig_fn(fd, buf, n, flags);})
IC_GENERIC(ssize_t, recv, (int fd, void *buf, size_t n, int flags),
           {ret = orig_fn(fd, buf, n, flags);})
IC_GENERIC(ssize_t, sendto, (int fd, const void *buf, size_t n, int flags,
			     const struct sockaddr *dest_addr, socklen_t addrlen),
           {ret = orig_fn(fd, buf, n, flags, dest_addr, addrlen);})
IC_GENERIC(ssize_t, recvfrom, (int fd, void *buf, size_t n, int flags,
			       struct sockaddr *src_addr, socklen_t *addrlen),
           {ret = orig_fn(fd, buf, n, flags, src_addr, addrlen);})
IC_GENERIC(ssize_t, sendmsg, (int fd, const struct msghdr *message, int flags),
	   {ret = orig_fn(fd, message, flags);})
IC_GENERIC(ssize_t, recvmsg, (int fd, struct msghdr *message, int flags),
	   {ret = orig_fn(fd, message, flags);})

IC_GENERIC(int, getsockopt, (int fd, int level, int optname, void *optval, socklen_t *optlen),
           {ret = orig_fn(fd, level, optname, optval, optlen);})
IC_GENERIC(int, setsockopt, (int fd, int level, int optname, const void *optval, socklen_t optlen),
           {ret = orig_fn(fd, level, optname, optval, optlen);})
IC_GENERIC(int, listen, (int fd, int n),
           {ret = orig_fn(fd, n);})
IC_GENERIC(int, accept, (int sockfd, struct sockaddr *addr, socklen_t *addrlen), {
    ret = orig_fn(sockfd, addr, addrlen);})
IC_GENERIC(int, accept4, (int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags), {
    ret = orig_fn(sockfd, addr, addrlen, flags);})
IC_GENERIC(int, shutdown, (int fd, int how),
           {ret = orig_fn(fd, how);})
IC_GENERIC(int, sockatmark, (int fd),
           {ret = orig_fn(fd);})
IC_GENERIC(int, isfdtype, (int fd, int fdtype),
           {ret = orig_fn(fd, fdtype);})

// mntent.h
IC_GENERIC(FILE*, setmntent, (const char *file, const char *mode),
           {ret = orig_fn(file, mode);})
IC_GENERIC(struct mntent *, getmntent, (FILE *stream),
           {ret = orig_fn(stream);})
IC_GENERIC(struct mntent *, getmntent_r, (FILE *stream, struct mntent *mntbuf, char *buf, int buflen),
           {ret = orig_fn(stream, mntbuf, buf, buflen);})
IC_GENERIC(int, addmntent, (FILE *stream,const struct mntent *mnt),
           {ret = orig_fn(stream, mnt);})
IC_GENERIC(int, endmntent, (FILE *stream),
           {ret = orig_fn(stream);})
IC_GENERIC(char*, hasmntopt, (const struct mntent *mnt, const char *opt),
           {ret = orig_fn(mnt, opt);})

//time.h
IC_GENERIC(clock_t, clock, (void),
           {ret = orig_fn();})
IC_GENERIC(time_t, time, (time_t *timer),
           {ret = orig_fn(timer);})

// ignore: difftime mktime strftime strptime strftime_l strptime_l asctime ctime
// tzname daylight timezone tzset localtime gmtime localtime_r gmtime_r ctime_r
// asctime_r timegm timelocal dysize getdate getdate_r

IC_GENERIC(int, stime, (const time_t *when),
           {ret = orig_fn(when);})
IC_GENERIC(int, nanosleep, (const struct timespec *req,
			    struct timespec *rem),
           {ret = orig_fn(req, rem);})
IC_GENERIC(int, clock_getres, (clockid_t clock_id, struct timespec *res),
           {ret = orig_fn(clock_id, res);})
IC_GENERIC(int, clock_gettime, (clockid_t clock_id, struct timespec *tp),
           {ret = orig_fn(clock_id, tp);})
IC_GENERIC(int, clock_settime, (clockid_t clock_id, const struct timespec *tp),
           {ret = orig_fn(clock_id, tp);})
IC_GENERIC(int, clock_nanosleep, (clockid_t clock_id, int flags,
				  const struct timespec *request,
				  struct timespec *remain),
           {ret = orig_fn(clock_id, flags, request, remain);})
IC_GENERIC(int, clock_getcpuclockid, (pid_t pid, clockid_t *clock_id),
           {ret = orig_fn(pid, clock_id);})
IC_GENERIC(int, timer_create, (clockid_t clock_id, struct sigevent *sevp,
			       timer_t *timerid),
           {ret = orig_fn(clock_id, sevp, timerid);})
IC_GENERIC(int, timer_delete, (timer_t timerid),
           {ret = orig_fn(timerid);})
IC_GENERIC(int, timer_settime, (timer_t timerid, int flags,
				const struct itimerspec *new_value,
				struct itimerspec * old_value),
           {ret = orig_fn(timerid, flags, new_value, old_value);})
IC_GENERIC(int, timer_gettime, (timer_t timerid, struct itimerspec *value),
           {ret = orig_fn(timerid, value);})
IC_GENERIC(int, timer_getoverrun, (timer_t timerid),
           {ret = orig_fn(timerid);})

// sys/time.h
IC_GENERIC(int, gettimeofday, (struct timeval *tv,
			       struct timezone *tz),
           {ret = orig_fn(tv, tz);})
IC_GENERIC(int, settimeofday, (const struct timeval *tv,
			       const struct timezone *tz),
           {ret = orig_fn(tv, tz);})
IC_GENERIC(int, adjtime, (const struct timeval *delta, struct timeval *olddelta),
           {ret = orig_fn(delta, olddelta);})
IC_GENERIC(int, getitimer, (int which, struct itimerval *curr_value),
           {ret = orig_fn(which, curr_value);})
IC_GENERIC(int, setitimer, (int which,const struct itimerval *new_value,
			    struct itimerval *old_value),
           {ret = orig_fn(which, new_value, old_value);})
IC(int, utimes, (const char *file, const struct timeval tvp[2]),
   {ret = orig_fn(file, tvp); intercept_utime(-1, file, false, ret);})
IC(int, lutimes, (const char *file, const struct timeval tvp[2]),
   {ret = orig_fn(file, tvp); intercept_utime(-1, file, true, ret);})
IC(int, futimes, (int fd, const struct timeval tvp[2]),
   {ret = orig_fn(fd, tvp);intercept_futime(fd, ret);})
IC(int, futimesat, (int fd, const char *file, const struct timeval times[2]),
   {ret = orig_fn(fd, file, times); intercept_utime(fd, file, false, ret);})

// sys/statfs.h
IC_GENERIC(int, statfs, (const char *file, struct statfs *buf),
           {ret = orig_fn(file, buf);})
IC_GENERIC(int, statfs64, (const char *file, struct statfs64 *buf),
           {ret = orig_fn(file, buf);})
IC_GENERIC(int, fstatfs, (int fildes, struct statfs *buf),
           {ret = orig_fn(fildes, buf);})
IC_GENERIC(int, fstatfs64, (int fildes, struct statfs64 *buf),
           {ret = orig_fn(fildes, buf);})

// sys/fstatvfs.h
IC_GENERIC(int, statvfs, (const char *file, struct statvfs *buf),
           {ret = orig_fn(file, buf);})
IC_GENERIC(int, statvfs64, (const char *file, struct statvfs64 *buf),
           {ret = orig_fn(file, buf);})
IC_GENERIC(int, fstatvfs, (int fildes, struct statvfs *buf),
           {ret = orig_fn(fildes, buf);})
IC_GENERIC(int, fstatvfs64, (int fildes, struct statvfs64 *buf),
           {ret = orig_fn(fildes, buf);})

