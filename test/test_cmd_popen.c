/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* Performs a popen() / pclose() pair.
 * The command (to be passed to "sh -c") is taken from the first command line parameter,
 * the pipe type is taken from the second.
 * Returns 0 (unless an error occurred), not the command's exit code. */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "need exactly 2 arguments\n");
    return 1;
  }

  FILE *f = popen(argv[1], argv[2]);
  if (f == NULL) {
    perror("popen");
    return 1;
  }

  char buf[4096];
  ssize_t n;
  if ((fcntl(fileno(f), F_GETFL) & O_ACCMODE) == O_RDONLY) {
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      fwrite(buf, 1, n, stdout);
    }
  } else {
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
      fwrite(buf, 1, n, f);
    }
  }

  if (pclose(f) < 0) {
    perror("pclose");
    return 1;
  }

  return 0;
}
