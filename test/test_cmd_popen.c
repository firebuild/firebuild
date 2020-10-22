/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* Performs a popen() / pclose() pair.
 * The command (to be passed to "sh -c") is taken from the first command line parameter.
 * Returns 0 (unless an error occurred), not the command's exit code. */

#include <stdio.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "need exactly 1 argument\n");
    return 1;
  }

  FILE *f = popen(argv[1], "w");
  if (f == NULL) {
    perror("popen");
    return 1;
  }

  if (pclose(f) < 0) {
    perror("pclose");
    return 1;
  }

  return 0;
}
