/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
  int fd, i;

  /* Set up some files for test_file_ops_[23]. */
  fd = creat("test_empty_1.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  fd = creat("test_empty_2.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  const char *msg = "Hello World!\n";
  fd = creat("test_nonempty_1.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  i = write(fd, msg, strlen(msg));
  if (i != strlen(msg)) {
    perror("write");
    exit(1);
  }
  close(fd);

  fd = creat("test_nonempty_2.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  i = write(fd, msg, strlen(msg));
  if (i != strlen(msg)) {
    perror("write");
    exit(1);
  }
  close(fd);

  /* Only create _1, and not _2. */
  fd = creat("test_maybe_exists_1.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  if (mkdir("test_directory", 0700) == -1) {
    perror("mkdir");
    exit(1);
  }

  /* Run part 2. */
  if (system("./test_file_ops_2") != 0) {
    exit(1);
  }

  /* Cleanup. */
  if (unlink("test_empty_1.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_empty_2.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_nonempty_1.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_nonempty_2.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_maybe_exists_1.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_maybe_exists_2.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (unlink("test_exclusive.txt") != 0) {
    perror("unlink");
    exit(1);
  }
  if (rmdir("test_directory") != 0) {
    perror("unlink");
    exit(1);
  }

  return 0;
}
