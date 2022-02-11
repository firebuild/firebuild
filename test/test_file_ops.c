/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/* Allow skipping this test since the ASAN & UBSAN build finds out that this code is incorrect. */
#ifndef SKIP_TEST_NULL_NONNULL_PARAMS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
  /* Call statx with invalid parameters, like cargo does. */
  statx(0, NULL, 0, STATX_ALL, NULL);
#pragma GCC diagnostic pop
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
