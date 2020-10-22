/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* Performs a system() call.
 * The command (to be passed to "sh -c") is taken from the first command line parameter.
 * Returns 0 (unless an error occurred), not the command's exit code. */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "need exactly 1 argument\n");
    return 1;
  }

  system(argv[1]);

  return 0;
}
