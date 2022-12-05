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

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main() {
  int fd, fd_dup, fd_dup2, fd_dup3, i, pipe_fds[2];
  struct stat st_buf;

  /* Close invalid file descriptior. Should not affect shortcutting. */
  close(-1);

  if (pipe2(pipe_fds, 0) != 0) {
    perror("pipe2" LOC);
    exit(1);
  } else {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
  }

  /* Set up some files for test_file_ops_[23]. */
  fd = creat("test_empty_1.txt", 0600);
  if (fd == -1) {
    perror("open" LOC);
    exit(1);
  }
  if (fstat(fd, &st_buf) != 0) {
    perror("stat" LOC);
    exit(1);
  }
  fd_dup = dup(fd);
  if (fd_dup == -1) {
    perror("dup" LOC);
    exit(1);
  }
  fd_dup2 = dup2(fd, fd_dup);
  if (fd_dup2 == -1) {
    perror("dup2" LOC);
    exit(1);
  }
  fd_dup3 = dup3(fd, fd_dup2, O_CLOEXEC);
  if (fd_dup3 == -1) {
    perror("dup3" LOC);
    exit(1);
  }
  close(fd);

  fd = creat("test_empty_2.txt", 0600);
  if (fd == -1) {
    perror("open" LOC);
    exit(1);
  }
  close(fd);

  const char *msg = "Hello World!\n";
  fd = creat("test_nonempty_1.txt", 0600);
  if (fd == -1) {
    perror("open" LOC);
    exit(1);
  }
  i = write(fd, msg, strlen(msg));
  if (i != (int) strlen(msg)) {
    perror("write" LOC);
    exit(1);
  }
  close(fd);

  fd = creat("test_nonempty_2.txt", 0600);
  if (fd == -1) {
    perror("open" LOC);
    exit(1);
  }
  i = write(fd, msg, strlen(msg));
  if (i != (int) strlen(msg)) {
    perror("write" LOC);
    exit(1);
  }
  close(fd);

  /* Only create _1, and not _2. */
  fd = creat("test_maybe_exists_1.txt", 0600);
  if (fd == -1) {
    perror("open" LOC);
    exit(1);
  }
  close(fd);

  DIR *d = opendir("./");
  if (d == NULL) {
    perror("opendir" LOC);
    exit(1);
  }
  closedir(d);

  if (mkdir("test_directory", 0700) == -1) {
    perror("mkdir" LOC);
    exit(1);
  }

  fd = open("test_directory", O_RDWR | O_TMPFILE, 0744);
  if (fd == -1 && errno != ENOTSUP) {
    perror("open(..., O_TMPFILE)" LOC);
    exit(1);
  }
  close(fd);

  char tmp_file[] = "tmpprefixXXXXXX";
  fd = mkstemp(tmp_file);
  if (fd == -1) {
    perror("mkstemp" LOC);
    exit(1);
  }
  close(fd);
  unlink(tmp_file);

  char tmp_dir[] = "./prefixXXXXXX";
  char *mkdtemp_ret = mkdtemp(tmp_dir);
  if (!mkdtemp_ret) {
    perror("mkdtemp" LOC);
    exit(1);
  }
  rmdir(mkdtemp_ret);

  fd = memfd_create("foo", MFD_CLOEXEC);
  if (fd == -1) {
    perror("memfd_create" LOC);
    exit(1);
  }
  close(fd);

  fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (fd == -1) {
    perror("timerfd_create" LOC);
    exit(1);
  }
  close(fd);

  fd = eventfd(0, EFD_CLOEXEC);
  if (fd == -1) {
    perror("eventfd" LOC);
    exit(1);
  }
  close(fd);

  sigset_t mask;
  sigemptyset(&mask);
  fd = signalfd(-1, &mask, SFD_CLOEXEC);
  if (fd == -1) {
    perror("signalfd" LOC);
    exit(1);
  }
  close(fd);

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd == -1) {
    perror("socket" LOC);
    exit(1);
  }
  close(fd);

  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pipe_fds) != 0) {
    perror("socketpair" LOC);
    exit(1);
  } else {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
  }

#ifdef STATX_TYPE
/* Allow skipping this test since the ASAN & UBSAN build finds out that this code is incorrect. */
#ifndef SKIP_TEST_NULL_NONNULL_PARAMS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
  /* Call statx with invalid parameters, like cargo does. */
  statx(0, NULL, 0, STATX_ALL, NULL);
#pragma GCC diagnostic pop
#endif
#endif

  if (stat(".", &st_buf) != 0) {
    perror("stat" LOC);
    exit(1);
  }

  if (stat("test_file_ops", &st_buf) != 0) {
    perror("stat" LOC);
    exit(1);
  }

  if (stat("stat_nonexistent", &st_buf) == 0) {
    fprintf(stderr, "stat() found unexpected file/dir");
    exit(1);
  }

  /* Run part 2. */
  if (system("./test_file_ops_2") != 0) {
    exit(1);
  }

  /* Cleanup. */
  if (unlink("test_empty_1.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_empty_2.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_nonempty_1.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_nonempty_2.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_maybe_exists_1.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_maybe_exists_2.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (unlink("test_exclusive.txt") != 0) {
    perror("unlink" LOC);
    exit(1);
  }
  if (rmdir("test_directory") != 0) {
    perror("unlink" LOC);
    exit(1);
  }

  return 0;
}
