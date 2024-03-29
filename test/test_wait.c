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

/* Test for #185: a system(), pclose(), wait(), waitpid() has to
 * wait not only for the process to terminate, but also for the
 * supervisor to do the administration, check the state of the
 * files as produced by that child. If we allowed the parent to
 * continue, it might modify the files and thus the wrong actions
 * would be recorded for the child, causing problems when we
 * shortcut it the next time. */

#include <fcntl.h>
#if __linux__
#include <features.h>
#endif
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#if __has_include(<sys/pidfd.h>)
#include <sys/pidfd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__GLIBC_PREREQ)
#define __GLIBC_PREREQ(a, b) 0
#endif

extern char **environ;

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main() {
  int fd;
  FILE *f, *f2;
  pid_t pid;
  siginfo_t info;

  /* Test waiting at system() */
  if ((fd = creat("test_wait_system.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
  if (system("exec touch test_wait_system.txt") != 0) {
    perror("system" LOC);
    exit(1);
  }
  if (unlink("test_wait_system.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at pclose() */
  if ((fd = creat("test_wait_pclose.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
  if ((f = popen("exec touch test_wait_pclose.txt", "w")) == NULL) {
    perror("popen" LOC);
    exit(1);
  }
  /* Run popen again to excercise supervisor tracking f to be closed in the new child. */
  if ((f2 = popen("exec touch test_wait_pclose.txt", "r")) == NULL) {
    perror("popen" LOC);
    exit(1);
  }
  if (pclose(f) != 0) {
    perror("pclose" LOC);
    exit(1);
  }
  if (pclose(f2) != 0) {
    perror("pclose" LOC);
    exit(1);
  }
  if (unlink("test_wait_pclose.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at wait() */
  if ((fd = creat("test_wait_wait.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
#if __GLIBC_PREREQ(2, 39)
  if (pidfd_spawn(&pid, "/usr/bin/touch",
#else
  if (posix_spawn(&pid, "/usr/bin/touch",
#endif
                  NULL, NULL,
                  (char *const[]){ "touch", "test_wait_wait.txt", NULL },
                  environ) != 0) {
    perror("posix_spawn" LOC);
    exit(1);
  }
#if __GLIBC_PREREQ(2, 39)
  pid = pidfd_getpid(pid);
#endif
  if (wait(NULL) != pid) {
    perror("wait" LOC);
    exit(1);
  }
  if (unlink("test_wait_wait.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at waitpid() */
  if ((fd = creat("test_wait_waitpid.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
#if __GLIBC_PREREQ(2, 39)
  if (pidfd_spawnp(&pid, "touch",
#else
  if (posix_spawnp(&pid, "touch",
#endif
                  NULL, NULL,
                  (char *const[]){ "touch", "test_wait_waitpid.txt", NULL },
                  environ) != 0) {
    perror("posix_spawn" LOC);
    exit(1);
  }
#if __GLIBC_PREREQ(2, 39)
  pid = pidfd_getpid(pid);
#endif
  if (waitpid(pid, NULL, 0) != pid) {
    perror("waitpid" LOC);
    exit(1);
  }
  if (unlink("test_wait_waitpid.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at wait3() */
  if ((fd = creat("test_wait_wait3.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
  if (posix_spawn(&pid, "/usr/bin/touch",
                  NULL, NULL,
                  (char *const[]){ "touch", "test_wait_wait3.txt", NULL },
                  environ) != 0) {
    perror("posix_spawn" LOC);
    exit(1);
  }
  if (wait3(NULL, 0, NULL) != pid) {
    perror("wait3" LOC);
    exit(1);
  }
  if (unlink("test_wait_wait3.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at wait4() */
  if ((fd = creat("test_wait_wait4.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
  if (posix_spawn(&pid, "/usr/bin/touch",
                  NULL, NULL,
                  (char *const[]){ "touch", "test_wait_wait4.txt", NULL },
                  environ) != 0) {
    perror("posix_spawn" LOC);
    exit(1);
  }
  if (wait4(pid, NULL, 0, NULL) != pid) {
    perror("wait4" LOC);
    exit(1);
  }
  if (unlink("test_wait_wait4.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  /* Test waiting at waitid() */
  if ((fd = creat("test_wait_waitid.txt", 0600)) < 0) {
    perror("open" LOC);
    exit(1);
  }
  if (close(fd) != 0) {
    perror("close" LOC);
    exit(1);
  }
  if (posix_spawn(&pid, "/usr/bin/touch",
                  NULL, NULL,
                  (char *const[]){"touch", "test_wait_waitid.txt", NULL},
                  environ) != 0) {
    perror("posix_spawn" LOC);
    exit(1);
  }
  if (waitid(P_PID, pid, &info, WEXITED) != 0) {
    perror("waitid" LOC);
    exit(1);
  }
  if (info.si_pid != pid) {
    fprintf(stderr, "waitid returned unexpected pid" LOC);
    exit(1);
  }
  if (unlink("test_wait_waitid.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  return 0;
}
