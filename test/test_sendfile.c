/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main() {
  int fd1, fd2;
  fd1 = open(".", O_RDWR | O_TMPFILE, 0644);
  if (fd1 == -1) {
    perror("open" LOC);
    exit(1);
  }
  fd2 = open("integration.bats", O_RDWR);
  if (fd2 == -1) {
    perror("open" LOC);
    close(fd1);
    exit(1);
  }

  if (sendfile(fd1, fd2, NULL, 10) == -1) {
    perror("sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  if (syscall(SYS_sendfile, fd1, fd2, NULL, 10) == -1) {
    perror("SYS_sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  if (copy_file_range(fd2, NULL, fd1, NULL, 10, 0) == -1) {
    perror("copy_file_range" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  close(fd1);
  close(fd2);

  return 0;
}
