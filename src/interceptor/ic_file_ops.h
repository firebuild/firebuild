/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

/**
 * This file is included several ways with different definitions for the
 * IC() macro.
 */

IC_VA(int, fcntl, (int fd, int cmd, ...), {
    va_list ap;
    va_start(ap, cmd);
    switch (cmd) {
      /* Commands the supervisor doesn't need to know about. */
      case F_GETFD:
      case F_GETFL:
      case F_SETFL:
      case F_GETLK:
      case F_SETLK:
      case F_SETLKW:
      case F_OFD_GETLK:
      case F_OFD_SETLK:
      case F_OFD_SETLKW:
      case F_GETOWN:
      case F_SETOWN:
      case F_GETOWN_EX:
      case F_SETOWN_EX:
      case F_GETSIG:
      case F_SETSIG:
      case F_GETLEASE:
      case F_SETLEASE:
      case F_NOTIFY:
      case F_GETPIPE_SZ:
      case F_SETPIPE_SZ:
      case F_ADD_SEALS:
      case F_GET_SEALS:
      case F_GET_RW_HINT:
      case F_SET_RW_HINT:
      case F_GET_FILE_RW_HINT:
      case F_SET_FILE_RW_HINT: {
        break;
      }

      /* Commands taking an arg that the supervisor needs to know about. */
      case F_DUPFD:
      case F_DUPFD_CLOEXEC:
      case F_SETFD: {
        int arg = va_arg(ap, int);
        intercept_fcntl(fd, cmd, arg, ret);
        break;
      }

      /* Commands that don't take an arg (or the arg doesn't matter to
       * the supervisor), but the supervisor needs to know about. This
       * includes all the unrecognized commands. Let's spell out the
       * recognized ones, rather than just catching them by "default",
       * for better readability. */
      default: {
        intercept_fcntl(fd, cmd, ret);
        break;
      }
    }
    va_end(ap);
  })

/**
 * Intercept open variants with varible length arg list.
 * mode is filled based on presence of O_CREAT flag
 */
#define IC_OPEN_VA(ret_type, name, pars, o_pars)        \
  IC(ret_type, name, pars,                              \
     {                                                  \
       mode_t mode = 0;                                 \
       va_list ap;                                      \
       va_start(ap, oflag);                             \
       mode = va_arg(ap, mode_t);                       \
       va_end(ap);                                      \
       bool created = false;                            \
       if ((oflag & O_CREAT) && !(oflag & O_EXCL)) {    \
         int err_saved = errno;                         \
         int tmp_fd;                                    \
         oflag &= ~O_CREAT;                             \
         if (-1 == (tmp_fd = orig_fn o_pars)) {         \
           if (errno == ENOENT) {                       \
             created = true;                            \
           }                                            \
         } else {                                       \
           ic_orig_close(tmp_fd);                       \
         }                                              \
         oflag |= O_CREAT;                              \
         errno = err_saved;                             \
       }                                                \
                                                        \
       ret = orig_fn o_pars;                            \
       if (!created) {                                  \
         intercept_open(file, oflag, mode, ret);        \
       } else {                                         \
         intercept_open(file, oflag, mode, true, ret);  \
       }                                                \
       clear_file_state(ret);                           \
     })


IC_OPEN_VA(int, open, (const char *file, int oflag, ...), (file, oflag, mode))

IC_OPEN_VA(int, open64, (const char *file, int oflag, ...), (file, oflag, mode))

IC_OPEN_VA(int, openat, (int fd, const char *file, int oflag, ...),
           (fd, file, oflag, mode))

IC_OPEN_VA(int, openat64, (int fd, const char *file, int oflag, ...),
           (fd, file, oflag, mode))

#define IC_FOPEN(name)                                                  \
  IC(FILE*, name, (const char *pathname, const char *mode),             \
     {                                                                  \
       int oflag = intercept_fopen_mode_to_open_flags_helper(mode);     \
       ret = orig_fn(pathname, mode);                                   \
       intercept_open(pathname, oflag, 0, ret?fileno(ret):-1);          \
       if (ret) {                                                       \
         clear_file_state(fileno(ret));                                 \
       }                                                                \
     })

IC_FOPEN(fopen)
IC_FOPEN(fopen64)

/* libc internal */
/** @cond INTERNAL */
IC(int, __libc_start_main, (int (*main)(int, char **, char **),
                            int argc, char **ubp_av,
                            void (*init)(void), void (*fini) (void),
                            void (*rtld_fini)(void), void (* stack_end)), {
    char * main_and_argv[2];
    main_and_argv[0] = reinterpret_cast<char *>(main);
    main_and_argv[1] = reinterpret_cast<char *>(ubp_av);
    intercept_on = false;
    insert_end_marker(__func__);
    orig_fn(firebuild_fake_main, argc, main_and_argv, init, fini,
            rtld_fini, stack_end);
    assert(0 && "fake_main must not return");
  })
/** @endcond */

/*  covered in unistd.h: lockf lockf64 */

/* unistd.h */

IC(int, close, (int fd), {
    if (fd != fb_sv_conn) {
      ret = orig_fn(fd);
      intercept_close(fd, ret);
      clear_file_state(fd);
    } else {
      /* we just skip closing our connection */
      ret = 0;
    }
  })

IC(int, access, (const char *name, int type), {
    ret = orig_fn(name, type); intercept_access(name, type, ret);})
IC(int, euidaccess, (const char *name, int type), {
    ret = orig_fn(name, type); intercept_eaccess(name, type, ret);})
IC(int, eaccess, (const char *name, int type), {
    ret = orig_fn(name, type); intercept_eaccess(name, type, ret);})
IC(int, faccessat, (int fd, const char *file, int type, int flag), {
    ret = orig_fn(fd, file, type, flag);
    intercept_faccessat(fd, file, type, flag, ret);})

// ignored: lseek lseek64
// those don't let new information enter the process

// TODO(rbalint) finish to handle stdio
IC(ssize_t, read, (int fd, void *buf, size_t nbytes), {
    ret = orig_fn(fd, buf, nbytes); intercept_read(fd, ret);})
IC(ssize_t, write, (int fd, const void *buf, size_t n), {
    ret = orig_fn(fd, buf, n); intercept_write(fd, ret);})
IC(ssize_t, pread, (int fd, void *buf, size_t nbytes, off_t offset), {
    ret = orig_fn(fd, buf, nbytes, offset); intercept_read(fd, ret);})
IC(ssize_t, pwrite, (int fd, const void *buf, size_t n, off_t offset), {
    ret = orig_fn(fd, buf, n, offset); intercept_write(fd, ret);})
IC(ssize_t, pread64, (int fd, void *buf, size_t nbytes, off_t offset), {
    ret = orig_fn(fd, buf, nbytes, offset); intercept_read(fd, ret);})
IC(ssize_t, pwrite64, (int fd, const void *buf, size_t n, off_t offset), {
    ret = orig_fn(fd, buf, n, offset); intercept_write(fd, ret);})
// TODO(rbalint) intercept to handle communication between forked children and parent
IC(int, pipe, (int pipedes[2]), {
    ret = orig_fn(pipedes);
    intercept_pipe2(pipedes, 0, ret);
    clear_file_state(pipedes[0]);
    clear_file_state(pipedes[1]); })
IC(int, pipe2, (int pipedes[2], int flags), {
    ret = orig_fn(pipedes, flags);
    intercept_pipe2(pipedes, flags, ret);
    clear_file_state(pipedes[0]);
    clear_file_state(pipedes[1]); })

// TODO(rbalint) those may affect output if the process measures time that way
// usually the calls can be ignored
IC_GENERIC(unsigned int, alarm, (unsigned int seconds), {
    ret = orig_fn(seconds);})
IC_GENERIC(unsigned int, sleep, (unsigned int seconds), {
    ret = orig_fn(seconds);})
IC_GENERIC(useconds_t, ualarm, (useconds_t value, useconds_t interval), {
    ret = orig_fn(value, interval);})
IC_GENERIC(int, usleep, (useconds_t useconds), {
    ret = orig_fn(useconds);})
IC_GENERIC(int, pause, (void), {
    ret = orig_fn();})

// TODO(rbalint) finish
IC(int, chown, (const char *file, uid_t owner, gid_t group), {
    ret = orig_fn(file, owner, group);
    intercept_chown(file, owner, group, ret); })
IC(int, fchown, (int fd, uid_t owner, gid_t group), {
    ret = orig_fn(fd, owner, group);
    intercept_fchown(fd, owner, group, ret); })
IC(int, lchown, (const char *file, uid_t owner, gid_t group), {
    ret = orig_fn(file, owner, group);
    intercept_lchown(file, owner, group, ret);
  })
IC(int, fchownat, (int fd, const char *file, uid_t owner,
                   gid_t group, int flag), {
     ret = orig_fn(fd, file, owner, group, flag);
     intercept_fchownat(fd, file, owner, group, flag, ret);})

IC(int, chdir, (const char *path), {
    ret = orig_fn(path);
    intercept_chdir(path, ret);
  })

IC(int, fchdir, (int fd), {
    ret = orig_fn(fd);
    intercept_fchdir(fd, ret);
  })

IC(char*, getcwd, (char *buf, size_t size), {
    ret = orig_fn(buf, size);
    intercept_getcwd(ret);
  })

IC(char*, get_current_dir_name, (void), {
    ret = orig_fn();
    intercept_getcwd(ret);
  })

IC(char*, getwd, (char *buf), {
    ret = orig_fn(buf);
    intercept_getcwd(ret);
  })

IC(int, dup, (int fd), {
    ret = orig_fn(fd);
    intercept_dup(fd, ret);
    copy_file_state(ret, fd);
  })
IC(int, dup2, (int fd, int fd2), {
    ret = orig_fn(fd, fd2);
    intercept_dup3(fd, fd2, 0, ret);
    if (ret != -1) {
      copy_file_state(fd2, fd);
    }
  })
IC(int, dup3, (int fd, int fd2, int flags), {
    ret = orig_fn(fd, fd2, flags);
    intercept_dup3(fd, fd2, flags, ret);
    if (ret != -1) {
      copy_file_state(fd2, fd);
    }
  })

IC(int, mkstemp, (char *pattern), {
  ret = orig_fn(pattern);  // This updates pattern in-place
  if (ret != -1) {
    intercept_open(pattern, O_RDWR|O_CREAT|O_EXCL, 0600, ret);
  }
})
IC(int, mkostemp, (char *pattern, int flags), {
  ret = orig_fn(pattern, flags);  // This updates pattern in-place
  if (ret != -1) {
    intercept_open(pattern, O_RDWR|O_CREAT|O_EXCL|flags, 0600, ret);
  }
})
IC(int, mkstemps, (char *pattern, int suffixlen), {
  ret = orig_fn(pattern, suffixlen);  // This updates pattern in-place
  if (ret != -1) {
    intercept_open(pattern, O_RDWR|O_CREAT|O_EXCL, 0600, ret);
  }
})
IC(int, mkostemps, (char *pattern, int suffixlen, int flags), {
  ret = orig_fn(pattern, suffixlen, flags);  // This updates pattern in-place
  if (ret != -1) {
    intercept_open(pattern, O_RDWR|O_CREAT|O_EXCL|flags, 0600, ret);
  }
})
IC(char*, mkdtemp, (char *pattern), {
  ret = orig_fn(pattern);  // This updates pattern in-place
  if (ret != NULL) {
    // FIXME intercept_mkdtemp()
    ret = ret;
  }
})

IC(int, execve, (const char *path, char *const argv[], char *const envp[]), {
    intercept_execve(false, path, -1, argv, envp);
    ret = orig_fn(path, argv, envp);
    intercept_execvfailed(ret);
  })
IC(int, fexecve, (int fd, char *const argv[], char *const envp[]), {
    intercept_execve(false, NULL, fd, argv, environ);
    ret = orig_fn(fd, argv, envp);
    intercept_execvfailed(ret);
  })
IC(int, execv, (const char *path, char *const argv[]), {
    intercept_execve(false, path, -1, argv, environ);
    ret = orig_fn(path, argv);
    intercept_execvfailed(ret);
  })

IC(int, execvp, (const char *file, char *const argv[]), {
    intercept_execve(true, file, -1, argv, environ);
    ret = orig_fn(file, argv);
    intercept_execvfailed(ret);
  })

IC(int, execvpe, (const char *file, char *const argv[],
                  char *const envp[]), {
     intercept_execve(true, file, -1, argv, envp);
     ret = orig_fn(file, argv, envp);
     intercept_execvfailed(ret);})

IC(int, system, (const char *cmd), {
    intercept_system(cmd);
    ret = orig_fn(cmd);
    intercept_system_ret(cmd, ret);})

IC(FILE*, popen, (const char *cmd, const char *type), {
    intercept_popen(cmd, type);
    ret = orig_fn(cmd, type);
    if (ret != NULL) {
      intercept_popen_parent(fileno(ret), type, ret);
    } else {
      intercept_popen_failed(cmd, ret);
    }
  })

IC(int, posix_spawn, (pid_t *pid, const char *path,
                      const posix_spawn_file_actions_t *file_actions,
                      const posix_spawnattr_t *attrp,
                      char *const argv[], char *const envp[]), {
    intercept_posix_spawn(path, false, argv, envp);
    ret = orig_fn(pid, path, file_actions, attrp, argv, envp);
    if (ret == 0) {
      intercept_posix_spawn_parent(*pid, ret);
    } else {
      intercept_posix_spawn_failed(argv, ret);
    }
  })
IC(int, posix_spawnp, (pid_t *pid, const char *file,
                       const posix_spawn_file_actions_t *file_actions,
                       const posix_spawnattr_t *attrp,
                       char *const argv[], char *const envp[]), {
    intercept_posix_spawn(file, true, argv, envp);
    ret = orig_fn(pid, file, file_actions, attrp, argv, envp);
    if (ret == 0) {
      intercept_posix_spawn_parent(*pid, ret);
    } else {
      intercept_posix_spawn_failed(argv, ret);
    }
  })

/* ignore: nice */

IC_VOID(void, exit, (int status), {
    intercept_exit();
    orig_fn(status);
    assert(0 && "exit must not return");
  })

IC_VOID(void, _exit, (int status), {
    intercept_underscore_exit(status);
    orig_fn(status);
    assert(0 && "_exit must not return");
  })
IC_VOID(void, _Exit, (int status), {
    intercept_underscore_exit(status);
    orig_fn(status);
    assert(0 && "_Exit must not return");
  })
IC_VOID(void, quick_exit, (int status), {
    intercept_underscore_exit(status);
    orig_fn(status);
    assert(0 && "quick_exit must not return");
  })

IC(long int, pathconf, (const char *path, int name), {
    ret = orig_fn(path, name); intercept_pathconf(path, name, ret);})
IC(long int, fpathconf, (int fd, int name), {
    ret = orig_fn(fd, name); intercept_fpathconf(fd, name, ret);})
IC(long int, sysconf, (int name), {
    ret = orig_fn(name); intercept_sysconf(name, ret);})
IC_GENERIC(size_t, confstr, (int name, char *buf, size_t len), {
    ret = orig_fn(name, buf, len);})
IC_GENERIC(pid_t, getpid, (void), {
    ret = orig_fn();})
IC_GENERIC(pid_t, getppid, (void), {
    ret = orig_fn();})
IC_GENERIC(pid_t, getpgrp, (void), {
    ret = orig_fn();})
IC_GENERIC(pid_t, getpgid, (pid_t pid), {
    ret = orig_fn(pid);})
IC_GENERIC(pid_t, __getpgid, (pid_t pid), {
    ret = orig_fn(pid);})
IC_GENERIC(int, setpgid, (pid_t pid, pid_t pgid), {
    ret = orig_fn(pid, pgid);})
IC_GENERIC(int, setpgrp, (void), {
    ret = orig_fn();})
IC_GENERIC(pid_t, setsid, (void), {
    ret = orig_fn();})
IC_GENERIC(pid_t, getsid, (pid_t pid), {
    ret = orig_fn(pid);})
IC_GENERIC(uid_t, getuid, (void), {
    ret = orig_fn();})
IC_GENERIC(uid_t, geteuid, (void), {
    ret = orig_fn();})
IC_GENERIC(gid_t, getgid, (void), {
    ret = orig_fn();})
IC_GENERIC(gid_t, getegid, (void), {
    ret = orig_fn();})
IC_GENERIC(int, getgroups, (int size, gid_t list[]), {
    ret = orig_fn(size, list);})
IC_GENERIC(int, group_member, (gid_t gid), {
    ret = orig_fn(gid);})
IC_GENERIC(int, setuid, (uid_t uid), {
    ret = orig_fn(uid);})
IC_GENERIC(int, setreuid, (uid_t ruid, uid_t euid), {
    ret = orig_fn(ruid, euid);})
IC_GENERIC(int, seteuid, (uid_t uid), {
    ret = orig_fn(uid);})
IC_GENERIC(int, setgid, (gid_t gid), {
    ret = orig_fn(gid);})
IC_GENERIC(int, setregid, (gid_t rgid, gid_t egid), {
    ret = orig_fn(rgid, egid);})
IC_GENERIC(int, setegid, (gid_t gid), {
    ret = orig_fn(gid);})
IC_GENERIC(int, getresuid, (uid_t *ruid, uid_t *euid, uid_t *suid), {
    ret = orig_fn(ruid, euid, suid);})
IC_GENERIC(int, getresgid, (gid_t *rgid, gid_t *egid, gid_t *sgid), {
    ret = orig_fn(rgid, egid, sgid);})
IC_GENERIC(int, setresuid, (uid_t ruid, uid_t euid, uid_t suid), {
    ret = orig_fn(ruid, euid, suid);})
IC_GENERIC(int, setresgid, (gid_t rgid, gid_t egid, gid_t sgid), {
    ret = orig_fn(rgid, egid, sgid);})

IC(pid_t, fork, (void), {
    ret = orig_fn();
    intercept_fork(ret);})

/*  probably never used */
IC_GENERIC(char*, ttyname, (int fd), {
    ret = orig_fn(fd);})
IC_GENERIC(int, ttyname_r, (int fd, char *buf, size_t buflen), {
    ret = orig_fn(fd, buf, buflen);})

/* ignore: isatty ttyslot */

/* TODO(rbalint) !!! */
IC(int, link, (const char *from, const char *to), {
    ret = orig_fn(from, to); intercept_link(from, to, ret);})
IC(int, linkat, (int fromfd, const char *from, int tofd,
                 const char *to, int flags), {
     ret = orig_fn(fromfd, from, tofd, to, flags);
     intercept_linkat(fromfd, from, tofd, to, flags, ret);})
IC(int, symlink, (const char *from, const char *to), {
    ret = orig_fn(from, to); intercept_symlink(from, to, ret);})
IC(ssize_t, readlink, (const char *path,
                       char *buf, size_t len), {
     ret = orig_fn(path, buf, len);
     intercept_readlink_helper(-1, path, buf, len, ret);})
IC(int, symlinkat, (const char *from, int tofd, const char *to), {
    ret = orig_fn(from, tofd, to);
    intercept_symlinkat(from, tofd, to, ret);})
IC(ssize_t, readlinkat, (int dirfd, const char *path,
                         char *buf, size_t len), {
     ret = orig_fn(dirfd, path, buf, len);
     intercept_readlink_helper(dirfd, path, buf, len, ret);})
IC(int, unlink, (const char *name), {
    ret = orig_fn(name);
    intercept_unlink(name, ret);})
IC(int, unlinkat, (int fd, const char *name, int flag), {
    ret = orig_fn(fd, name, flag);
    intercept_unlinkat(fd, name, flag, ret);})
IC(int, rmdir, (const char *path), {
    ret = orig_fn(path);
    intercept_rmdir(path, ret);})

IC_GENERIC(pid_t, tcgetpgrp, (int fd), {
    ret = orig_fn(fd);})
IC_GENERIC(int, tcsetpgrp, (int fd, pid_t pgrp_id), {
    ret = orig_fn(fd, pgrp_id);})
IC_GENERIC(char*, getlogin, (void), {
    ret = orig_fn();})
IC_GENERIC(int, getlogin_r, (char *name, size_t name_len), {
    ret = orig_fn(name, name_len);})
IC_GENERIC(int, setlogin, (const char *name), {
    ret = orig_fn(name);})
IC(int, gethostname, (char *name, size_t len), {
    ret = orig_fn(name, len);
    intercept_gethostname(name, len, ret);
  })
IC_GENERIC(int, sethostname, (const char *name, size_t len), {
    ret = orig_fn(name, len);})
IC_GENERIC(int, sethostid, (long int id), {
    ret = orig_fn(id);})
IC(int, getdomainname, (char *name, size_t len), {
    ret = orig_fn(name, len);
    intercept_getdomainname(name, len, ret);
  })
IC_GENERIC(int, setdomainname, (const char *name, size_t len), {
    ret = orig_fn(name, len);})

IC_GENERIC(int, vhangup, (void), {
    ret = orig_fn();})
IC_GENERIC(int, revoke, (const char *file), {
    ret = orig_fn(file);})
IC_GENERIC(int, profil, (unsigned short int *sample_buffer, size_t size,
                         size_t offset, unsigned int scale), {
             ret = orig_fn(sample_buffer, size, offset, scale);})
IC_GENERIC(int, acct, (const char *filename), {
    ret = orig_fn(filename);})

IC_GENERIC(char*, getusershell, (void), {
    ret = orig_fn();})
IC_GENERIC_VOID(void, endusershell, (void), {orig_fn();})
IC_GENERIC_VOID(void, setusershell, (void), {orig_fn();})

IC_GENERIC(int, daemon, (int nochdir, int noclose), {
    ret = orig_fn(nochdir, noclose);})
IC_GENERIC(int, chroot, (const char *path), {
    ret = orig_fn(path);})
/* this may be ignored */
IC_GENERIC(char*, getpass, (const char *prompt), {
    ret = orig_fn(prompt);})

// ignore fsync

IC_GENERIC(long int, gethostid, (void), {
    ret = orig_fn();})

// ignore: sync, getpagesize (calloc calls it)

IC_GENERIC(int, getdtablesize, (void), {
    ret = orig_fn();})
IC(int, truncate, (const char *file, off_t length), {
    ret = orig_fn(file, length);
    intercept_truncate(file, length, ret);
  })
IC(int, truncate64, (const char *file, off64_t length), {
    ret = orig_fn(file, length);
    intercept_truncate(file, length, ret);
  })
IC(int, ftruncate, (int fd, off_t length), {
    ret = orig_fn(fd, length);
    intercept_ftruncate(fd, length, ret);
  })
IC(int, ftruncate64, (int fd, off64_t length), {
    ret = orig_fn(fd, length);
    intercept_ftruncate(fd, length, ret);
  })

/* ignore: brk sbrk */

IC(long int, syscall, (long int sysno, ...), {
    void *args = __builtin_apply_args();
    void const * const result = __builtin_apply((void (*)(...))(void *)orig_fn,
                                                args, 100);
    ret = *(long int *)result;
    intercept_syscall(sysno, ret);})

/* we probably won't use offset in supervisor's logic */
IC(int, lockf, (int fd, int cmd, off_t len), {
    ret = orig_fn(fd, cmd, len);
    intercept_lockf(fd, cmd, /* len,*/ ret);
  })
IC(int, lockf64, (int fd, int cmd, off64_t len), {
    ret = orig_fn(fd, cmd, len);
    intercept_lockf(fd, cmd, /*len,*/ ret);
  })

/* ignored: fdatasync crypt encrypt swab */


// TODO(rbalint) intercept fns
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

IC_GENERIC(int, chmod, (const char *file, mode_t mode), {
    ret = orig_fn(file, mode); /*intercept_();*/})
IC_GENERIC(int, lchmod, (const char *file, mode_t mode), {
    ret = orig_fn(file, mode); /*intercept_();*/})
IC_GENERIC(int, fchmod, (int fd, mode_t mode), {
    ret = orig_fn(fd, mode); /*intercept_();*/})
IC_GENERIC(int, fchmodat, (int fd, const char *file, mode_t mode, int flag), {
    ret = orig_fn(fd, file, mode, flag); /*intercept_();*/})
IC_GENERIC(mode_t, umask, (mode_t mask), {
    ret = orig_fn(mask); /*intercept_();*/})
IC_GENERIC(mode_t, getumask, (void), {
    ret = orig_fn(); /*intercept_();*/})
IC_GENERIC(int, mkdir, (const char *path, mode_t mode), {
    ret = orig_fn(path, mode); /*intercept_();*/})
IC_GENERIC(int, mkdirat, (int fd, const char *path, mode_t mode), {
    ret = orig_fn(fd, path, mode); /*intercept_();*/})
IC_GENERIC(int, mknod, (const char *path, mode_t mode, dev_t dev), {
    ret = orig_fn(path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, mknodat, (int fd, const char *path, mode_t mode, dev_t dev), {
    ret = orig_fn(fd, path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, mkfifo, (const char *path, mode_t mode), {
    ret = orig_fn(path, mode); /*intercept_();*/})
IC_GENERIC(int, mkfifoat, (int fd, const char *path, mode_t mode), {
    ret = orig_fn(fd, path, mode); /*intercept_();*/})
IC(int, utimensat, (int fd, const char *path, const struct timespec times[2],
                    int flags), {
     ret = orig_fn(fd, path, times, flags);
     intercept_utime(fd, path, (flags & AT_SYMLINK_NOFOLLOW)?true:false, ret);})
IC(int, futimens, (int fd, const struct timespec times[2]), {
    ret = orig_fn(fd, times); intercept_futime(fd, ret);})
IC_GENERIC(int, __fxstat, (int ver, int fildes, struct stat *stat_buf), {
    ret = orig_fn(ver, fildes, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __xstat, (int ver, const char *filename,
                          struct stat *stat_buf), {
             ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __lxstat, (int ver, const char *filename,
                           struct stat *stat_buf), {
             ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __fxstatat, (int ver, int fildes, const char *filename,
                             struct stat *stat_buf, int flag), {
             ret = orig_fn(ver, fildes, filename, stat_buf, flag);
             /* intercept_(); */})
IC_GENERIC(int, __fxstat64, (int ver, int fildes, struct stat64 *stat_buf), {
    ret = orig_fn(ver, fildes, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __xstat64, (int ver, const char *filename,
                            struct stat64 *stat_buf), {
             ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __lxstat64, (int ver, const char *filename,
                             struct stat64 *stat_buf), {
             ret = orig_fn(ver, filename, stat_buf); /*intercept_();*/})
IC_GENERIC(int, __fxstatat64, (int ver, int fildes, const char *filename,
                               struct stat64 *stat_buf, int flag), {
             ret = orig_fn(ver, fildes, filename, stat_buf, flag);
             /* intercept_(); */})
IC_GENERIC(int, xmknod, (int ver, const char *path, mode_t mode, dev_t *dev), {
    ret = orig_fn(ver, path, mode, dev); /*intercept_();*/})
IC_GENERIC(int, xmknodat, (int ver, int fd, const char *path,
                           mode_t mode, dev_t *dev), {
             ret = orig_fn(ver, fd, path, mode, dev); /*intercept_();*/})

// stdio.h
IC(int, remove, (const char *filename), {
    ret = orig_fn(filename); intercept_remove(filename, ret);})
IC(int, rename, (const char *oldpath, const char *newpath), {
    ret = orig_fn(oldpath, newpath); intercept_rename(oldpath, newpath, ret);})
IC(int, renameat, (int oldfd, const char *oldpath, int newfd,
                   const char *newpath), {
     ret = orig_fn(oldfd, oldpath, newfd, newpath);
     intercept_renameat(oldfd, oldpath, newfd, newpath, ret);})
IC_GENERIC(FILE*, tmpfile, (void), {
    ret = orig_fn();})
IC_GENERIC(FILE*, tmpfile64, (void), {
    ret = orig_fn();})
// ignore: tmpnam tmpnam_r tempnam
IC(int, fclose, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    if (stream_fileno != fb_sv_conn) {
      intercept_close(stream_fileno, (ret == EOF)?-1:ret);}})
// ignore: fflush fflush_unlocked
IC(int, fcloseall, (void), {
    ret = orig_fn();
    intercept_fcloseall((ret == EOF)?-1:ret);})
IC(FILE*, freopen, (const char *filename, const char *modes, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(filename, modes, stream);
    intercept_freopen(filename, modes, stream_fileno, (ret)?fileno(ret):(-1));})
IC(FILE*, freopen64, (const char *filename, const char *modes, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(filename, modes, stream);
    intercept_freopen(filename, modes, stream_fileno, (ret)?fileno(ret):(-1));})

// ignore: fdopen fdopendir fopencookie fmemopen open_memstream setbuf setbuffer
// setlinebuf setvbuf -- these are functions that do glibc internal administration only,
// without actual file operations.

IC_VA(int, fprintf, (FILE * stream, const char * format, ...), {
    (void)format;
    intercept_fwrite(stream, ret);
    /* TODO(rbalint) check result and std fds */ })
IC_VA(int, printf, (const char * format, ...), {
    (void)format;
    intercept_write(STDOUT_FILENO, ret);
    /* TODO(rbalint) check result and store std out */ })
// ignore: sprintf
IC(int, vfprintf, (FILE * stream, const char * format,
#if __GLIBC_PREREQ (2, 28)
                   va_list arg
#else
                   _G_va_list arg
#endif
                                 ), {
    ret = orig_fn(stream, format, arg);
    intercept_fwrite(stream, ret);
    /* TODO(rbalint) check result and std out */ })
IC(int, vprintf, (const char * format,
#if __GLIBC_PREREQ (2, 28)
                  va_list arg
#else
                  _G_va_list arg
#endif
                                ), {
    ret = orig_fn(format, arg);
    intercept_write(STDOUT_FILENO, ret);
    /* TODO(rbalint) check result and std out */ })
// ignore: vsprintf snprintf vsnprintf vasprintf asprintf
IC(int, vdprintf, (int fd, const char * fmt,
#if __GLIBC_PREREQ (2, 28)
                   va_list arg
#else
                   _G_va_list arg
#endif
                                 ), {
    ret = orig_fn(fd, fmt, arg);
    intercept_write(fd, ret);
    /* TODO(rbalint) check result and std fds */ })
IC_VA(int, dprintf, (int fd, const char * fmt, ...), {
    intercept_write(fd, ret);
    (void)fmt;
    /* TODO(rbalint) check result and std fds */ })

// generate ISO C99 or POSIX compliant variant
#define IC_VA_WITH_C99(ret_type, name, parameters, body)           \
  IC_VA(ret_type, name, parameters, body)                          \
  IC_VA(ret_type, __isoc99_##name, parameters, body)
#define IC_WITH_C99(ret_type, name, parameters, body)           \
  IC(ret_type, name, parameters, body)                          \
  IC(ret_type, __isoc99_##name, parameters, body)

IC_VA_WITH_C99(int, fscanf, (FILE * stream, const char * format, ...), {
    (void)format;
    intercept_fread(stream, ret);
    /* TODO(rbalint) check result and std fds */ })
IC_VA_WITH_C99(int, scanf, (const char * format, ...), {
    (void)format;
    intercept_fread(stdin, ret);
    /* TODO(rbalint) check result and std in */ })
// ignore: sscanf
// ignore: sscanf
IC_WITH_C99(int, vfscanf, (FILE * stream, const char * format,
#if __GLIBC_PREREQ (2, 28)
                           va_list arg
#else
                           _G_va_list arg
#endif
                                         ), {
    ret = orig_fn(stream, format, arg);
    intercept_fread(stream, ret);
    /* TODO(rbalint) check result and std fds */ })
IC_WITH_C99(int, vscanf, (const char * format,
#if __GLIBC_PREREQ (2, 28)
                          va_list arg
#else
                          _G_va_list arg
#endif
                                        ), {
    ret = orig_fn(format, arg);
    intercept_fread(stdin, ret);
    /* TODO(rbalint) check result and std in */ })
// ignore: vsscanf

/**
 * generate two intercepted functions, one for name and one for
 * name_unlocked */
#define IC_WITH_UNLOCKED(ret_type, name, parameters, body)      \
  IC(ret_type, name, parameters, body)                          \
  IC(ret_type, name##_unlocked, parameters, body)

IC_WITH_UNLOCKED(size_t, fread, (void *ptr, size_t size, size_t nmemb,
                                 FILE *stream), {
                   ret = orig_fn(ptr, size, nmemb, stream);
                   intercept_fread(stream, ret);})
IC_WITH_UNLOCKED(size_t, fwrite, (const void *ptr, size_t size, size_t nmemb,
                                  FILE *stream), {
                   ret = orig_fn(ptr, size, nmemb, stream);
                   intercept_fwrite(stream, ret);})

IC_WITH_UNLOCKED(int, fputc, (int c, FILE *stream), {
    ret = orig_fn(c, stream);
    intercept_fwrite(stream, ret);})
IC_WITH_UNLOCKED(wint_t, fputwc, (wchar_t c, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == WEOF)?-1:ret);})
IC_WITH_UNLOCKED(int, fputs, (const char *s, FILE *stream), {
    ret = orig_fn(s, stream);
    intercept_fwrite(stream, ret);})
IC_WITH_UNLOCKED(int, putc, (int c, FILE *stream), {
    ret = orig_fn(c, stream);
    intercept_fwrite(stream, ret);})
IC_WITH_UNLOCKED(wint_t, putwc, (wchar_t c, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(c, stream);
    intercept_write(stream_fileno, (ret == WEOF)?-1:ret);})
IC_WITH_UNLOCKED(int, putchar, (int c), {
    ret = orig_fn(c);
    intercept_write(STDOUT_FILENO, (ret == EOF)?-1:ret);})
IC_WITH_UNLOCKED(wint_t, putwchar, (wchar_t c), {
    ret = orig_fn(c);
    intercept_write(STDOUT_FILENO, (ret == WEOF)?-1:ret);})
IC(int, puts, (const char *s), {
    ret = orig_fn(s);
    intercept_write(STDOUT_FILENO, (ret == EOF)?-1:ret);})

IC_WITH_UNLOCKED(int, fgetc, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, fgetwc, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == WEOF)?-1:2);})
IC_WITH_UNLOCKED(char*, fgets, (char *s, int n, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(s, n, stream);
    intercept_read(stream_fileno, ret?strlen(ret):-1);})
// TODO(rbalint) _IO_getc and friends?
IC_WITH_UNLOCKED(int, getc, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, getwc, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == WEOF)?-1:2);})
IC_WITH_UNLOCKED(int, getchar, (void), {
    ret = orig_fn();
    intercept_read(STDOUT_FILENO, (ret == EOF)?-1:1);})
IC_WITH_UNLOCKED(wint_t, getwchar, (void), {
    ret = orig_fn();
    intercept_read(STDOUT_FILENO, (ret == WEOF)?-1:2);})
/* should be never used, see man gets */
IC(char*, gets, (char *s), {
    ret = orig_fn(s);
    intercept_read(STDOUT_FILENO, ret?strlen(ret):-1);})
IC(int, getw, (FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(stream);
    intercept_read(stream_fileno, (ret == EOF)?-1:1);})
IC(int, putw, (int w, FILE *stream), {
    int stream_fileno = (stream)?fileno(stream):-1;
    ret = orig_fn(w, stream);
    intercept_write(stream_fileno, (ret == EOF)?-1:ret);})
IC(
#if __GLIBC_PREREQ (2, 28)
   ssize_t
#else
   _IO_ssize_t
#endif
              , getdelim, (char ** lineptr, size_t * n, int delimiter,
                           FILE * stream), {
     ret = orig_fn(lineptr, n, delimiter, stream);
     intercept_fread(stream, (ret == EOF)?-1:1);
     /* TODO(rbalint) check result and std fds */ })
IC(
#if __GLIBC_PREREQ (2, 28)
   ssize_t
#else
   _IO_ssize_t
#endif
              , __getdelim, (char ** lineptr, size_t * n, int delimiter,
                             FILE * stream), {
     ret = orig_fn(lineptr, n, delimiter, stream);
     intercept_fread(stream, (ret == EOF)?-1:1);
     /* TODO(rbalint) check result and std fds */ })
IC(
#if __GLIBC_PREREQ (2, 28)
   ssize_t
#else
   _IO_ssize_t
#endif
              , getline, (char ** lineptr, size_t * n, FILE * stream), {
    ret = orig_fn(lineptr, n, stream);
    intercept_fread(stream, (ret == EOF)?-1:1);
    /* TODO(rbalint) check result and std fds */ })
/* TODO(rbalint)  invalidate shortcut */
IC_GENERIC(int, ungetc, (int c, FILE * stream), {
    ret = orig_fn(c, stream);})
// ignore fseek ftell rewind fseeko ftello fseeko64 ftello64 fgetpos fsetpos
// fgetpos64 fsetpos64 clearerr feof ferror clearerr_unlocked feof_unlocked
// ferror_unlocked perror fileno fileno_unlocked

IC_GENERIC(char *, ctermid, (char *s), {
    ret = orig_fn(s);})
IC_GENERIC(char *, cuserid, (char *s), {
    ret = orig_fn(s);})
// ignore flockfile ftrylockfile funlockfile

// dlfcn.h
IC(void*, dlopen, (const char *filename, int flag), {
    // dlopen may cause new intercepted calls
    intercept_on = false;
    ret = orig_fn(filename, flag);
    intercept_on = true;
    intercept_dlopen(filename, flag, ret);
  })

// dirent.h
IC(DIR *, opendir, (const char *name), {
    ret = orig_fn(name); intercept_opendir(name, ret);})
IC_GENERIC(int, closedir, (DIR *dirp), {
    ret = orig_fn(dirp);})
IC_GENERIC(struct dirent *, readdir, (DIR *dirp), {
    ret = orig_fn(dirp);})
IC_GENERIC(struct dirent64 *, readdir64, (DIR *dirp), {
    ret = orig_fn(dirp);})
IC_GENERIC(int, readdir_r, (DIR *dirp, struct dirent *entry,
                            struct dirent **result), {
             ret = orig_fn(dirp, entry, result);})
IC_GENERIC(int, readdir64_r, (DIR *dirp, struct dirent64 *entry,
                              struct dirent64 **result), {
             ret = orig_fn(dirp, entry, result);})
IC_GENERIC_VOID(void, rewinddir, (DIR *dirp), {
    orig_fn(dirp);})
IC_GENERIC_VOID(void, seekdir, (DIR *dirp, long int pos), {
    orig_fn(dirp, pos);})
IC_GENERIC(long int, telldir, (DIR *dirp), {
    ret = orig_fn(dirp);})
IC_GENERIC(int, dirfd, (DIR *dirp), {
    ret = orig_fn(dirp);})
// ignore scandir scandir64 alphasort
IC_GENERIC(ssize_t, getdirentries, (int fd, char *buf, size_t nbytes,
                                    off_t *basep), {
             ret = orig_fn(fd, buf, nbytes, basep);})
IC_GENERIC(ssize_t, getdirentries64, (int fd, char *buf, size_t nbytes,
                                      off64_t *basep), {
             ret =  orig_fn(fd, buf, nbytes, basep);})
// ignore versionsort versionsort64



// socket.h
IC_GENERIC(int, socket, (int domain, int type, int protocol), {
    ret = orig_fn(domain, type, protocol);})
IC_GENERIC(int, socketpair, (int domain, int type, int protocol, int sv[2]), {
    ret = orig_fn(domain, type, protocol, sv);})
IC_GENERIC(int, bind, (int fd, const struct sockaddr *addr, socklen_t len), {
    ret = orig_fn(fd, addr, len);})
IC_GENERIC(int, getsockname, (int fd, struct sockaddr *addr,
                              socklen_t *addrlen), {
             ret = orig_fn(fd, addr, addrlen);})
IC_GENERIC(int, connect, (int fd, const struct sockaddr *addr, socklen_t len), {
    ret = orig_fn(fd, addr, len);})
IC_GENERIC(int, getpeername, (int fd, struct sockaddr *addr,
                              socklen_t *addrlen), {
             ret = orig_fn(fd, addr, addrlen);})
IC_GENERIC(ssize_t, send, (int fd, const void *buf, size_t n, int flags), {
    ret = orig_fn(fd, buf, n, flags);})
IC_GENERIC(ssize_t, recv, (int fd, void *buf, size_t n, int flags), {
    ret = orig_fn(fd, buf, n, flags);})
IC_GENERIC(ssize_t, sendto, (int fd, const void *buf, size_t n, int flags,
                             const struct sockaddr *dest_addr,
                             socklen_t addrlen), {
             ret = orig_fn(fd, buf, n, flags, dest_addr, addrlen);})
IC_GENERIC(ssize_t, recvfrom, (int fd, void *buf, size_t n, int flags,
                               struct sockaddr *src_addr, socklen_t *addrlen), {
             ret = orig_fn(fd, buf, n, flags, src_addr, addrlen);})
IC_GENERIC(ssize_t, sendmsg, (int fd, const struct msghdr *message,
                              int flags), {
             ret = orig_fn(fd, message, flags);})
IC_GENERIC(ssize_t, recvmsg, (int fd, struct msghdr *message, int flags), {
    ret = orig_fn(fd, message, flags);})

IC_GENERIC(int, getsockopt, (int fd, int level, int optname, void *optval,
                             socklen_t *optlen), {
             ret = orig_fn(fd, level, optname, optval, optlen);})
IC_GENERIC(int, setsockopt, (int fd, int level, int optname, const void *optval,
                             socklen_t optlen), {
             ret = orig_fn(fd, level, optname, optval, optlen);})
IC_GENERIC(int, listen, (int fd, int n), {
    ret = orig_fn(fd, n);})
IC_GENERIC(int, accept, (int sockfd, struct sockaddr *addr,
                         socklen_t *addrlen), {
             ret = orig_fn(sockfd, addr, addrlen);})
IC_GENERIC(int, accept4, (int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                          int flags), {
             ret = orig_fn(sockfd, addr, addrlen, flags);})
IC_GENERIC(int, shutdown, (int fd, int how), {
    ret = orig_fn(fd, how);})
IC_GENERIC(int, sockatmark, (int fd), {
    ret = orig_fn(fd);})
IC_GENERIC(int, isfdtype, (int fd, int fdtype), {
    ret = orig_fn(fd, fdtype);})

// mntent.h
IC_GENERIC(FILE*, setmntent, (const char *file, const char *mode), {
    ret = orig_fn(file, mode);})
IC_GENERIC(struct mntent *, getmntent, (FILE *stream), {
    ret = orig_fn(stream);})
IC_GENERIC(struct mntent *, getmntent_r, (FILE *stream, struct mntent *mntbuf,
                                          char *buf, int buflen), {
             ret = orig_fn(stream, mntbuf, buf, buflen);})
IC_GENERIC(int, addmntent, (FILE *stream, const struct mntent *mnt), {
    ret = orig_fn(stream, mnt);})
IC_GENERIC(int, endmntent, (FILE *stream), {
    ret = orig_fn(stream);})
IC_GENERIC(char*, hasmntopt, (const struct mntent *mnt, const char *opt), {
    ret = orig_fn(mnt, opt);})

// time.h
IC_GENERIC(clock_t, clock, (void), {
    ret = orig_fn();})
IC_GENERIC(time_t, time, (time_t *timer), {
    ret = orig_fn(timer);})

// ignore: difftime mktime strftime strptime strftime_l strptime_l asctime ctime
// tzname daylight timezone tzset localtime gmtime localtime_r gmtime_r ctime_r
// asctime_r timegm timelocal dysize getdate getdate_r

IC_GENERIC(int, stime, (const time_t *when), {
    ret = orig_fn(when);})
IC_GENERIC(int, nanosleep, (const struct timespec *req,
                            struct timespec *rem), {
             ret = orig_fn(req, rem);})
IC_GENERIC(int, clock_getres, (clockid_t clock_id, struct timespec *res), {
    ret = orig_fn(clock_id, res);})
IC_GENERIC(int, clock_gettime, (clockid_t clock_id, struct timespec *tp), {
    ret = orig_fn(clock_id, tp);})
IC_GENERIC(int, clock_settime, (clockid_t clock_id,
                                const struct timespec *tp), {
             ret = orig_fn(clock_id, tp);})
IC_GENERIC(int, clock_nanosleep, (clockid_t clock_id, int flags,
                                  const struct timespec *request,
                                  struct timespec *remain), {
             ret = orig_fn(clock_id, flags, request, remain);})
IC_GENERIC(int, clock_getcpuclockid, (pid_t pid, clockid_t *clock_id), {
    ret = orig_fn(pid, clock_id);})
IC_GENERIC(int, timer_create, (clockid_t clock_id, struct sigevent *sevp,
                               timer_t *timerid), {
             ret = orig_fn(clock_id, sevp, timerid);})
IC_GENERIC(int, timer_delete, (timer_t timerid), {
    ret = orig_fn(timerid);})
IC_GENERIC(int, timer_settime, (timer_t timerid, int flags,
                                const struct itimerspec *new_value,
                                struct itimerspec * old_value), {
             ret = orig_fn(timerid, flags, new_value, old_value);})
IC_GENERIC(int, timer_gettime, (timer_t timerid, struct itimerspec *value), {
    ret = orig_fn(timerid, value);})
IC_GENERIC(int, timer_getoverrun, (timer_t timerid), {
    ret = orig_fn(timerid);})

// sys/time.h
IC_GENERIC(int, gettimeofday, (struct timeval *tv,
                               struct timezone *tz), {
             ret = orig_fn(tv, tz);})
IC_GENERIC(int, settimeofday, (const struct timeval *tv,
                               const struct timezone *tz), {
             ret = orig_fn(tv, tz);})
IC_GENERIC(int, adjtime, (const struct timeval *delta,
                          struct timeval *olddelta), {
             ret = orig_fn(delta, olddelta);})
IC_GENERIC(int, getitimer, (int which, struct itimerval *curr_value), {
    ret = orig_fn(which, curr_value);})
IC_GENERIC(int, setitimer, (int which, const struct itimerval *new_value,
                            struct itimerval *old_value), {
             ret = orig_fn(which, new_value, old_value);})
IC(int, utimes, (const char *file, const struct timeval tvp[2]), {
    ret = orig_fn(file, tvp); intercept_utime(-1, file, false, ret);})
IC(int, lutimes, (const char *file, const struct timeval tvp[2]), {
    ret = orig_fn(file, tvp); intercept_utime(-1, file, true, ret);})
IC(int, futimes, (int fd, const struct timeval tvp[2]), {
    ret = orig_fn(fd, tvp); intercept_futime(fd, ret);})
IC(int, futimesat, (int fd, const char *file, const struct timeval times[2]), {
    ret = orig_fn(fd, file, times); intercept_utime(fd, file, false, ret);})

// sys/statfs.h
IC_GENERIC(int, statfs, (const char *file, struct statfs *buf), {
    ret = orig_fn(file, buf);})
IC_GENERIC(int, statfs64, (const char *file, struct statfs64 *buf), {
    ret = orig_fn(file, buf);})
IC_GENERIC(int, fstatfs, (int fildes, struct statfs *buf), {
    ret = orig_fn(fildes, buf);})
IC_GENERIC(int, fstatfs64, (int fildes, struct statfs64 *buf), {
    ret = orig_fn(fildes, buf);})

// sys/fstatvfs.h
IC_GENERIC(int, statvfs, (const char *file, struct statvfs *buf), {
    ret = orig_fn(file, buf);})
IC_GENERIC(int, statvfs64, (const char *file, struct statvfs64 *buf), {
    ret = orig_fn(file, buf);})
IC_GENERIC(int, fstatvfs, (int fildes, struct statvfs *buf), {
    ret = orig_fn(fildes, buf);})
IC_GENERIC(int, fstatvfs64, (int fildes, struct statvfs64 *buf), {
    ret = orig_fn(fildes, buf);})

// sys/ioctl.h
/* ioctl() may have a third argument */
IC_GENERIC(int, ioctl, (int fd, unsigned long int request, ...), {
    va_list ap;
    va_start(ap, request);
    void *argp = va_arg(ap, void*);
    va_end(ap);
    ret = orig_fn(fd, request, argp);})

// sys/timex.h
IC_GENERIC(int, adjtimex, (struct timex *ntx), {
    ret = orig_fn(ntx);})
IC_GENERIC(int, ntp_gettime, (struct ntptimeval *ntv), {
    ret = orig_fn(ntv);})
IC_GENERIC(int, ntp_gettimex, (struct ntptimeval *ntv), {
    ret = orig_fn(ntv);})
IC_GENERIC(int, ntp_adjtime, (struct timex *tntx), {
    ret = orig_fn(tntx);})
