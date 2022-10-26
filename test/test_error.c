/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>

void atexit_handler() {
  fprintf(stderr, "atexit_handler\n");
}

int main() {
  atexit(atexit_handler);

  error(0, ENOENT, "error%d", 1);
  error(0, EACCES, "error%d", 2);
  error(1, ENOENT, "error%d", 3);
  /* should not reach here */
  error(1, EACCES, "error%d", 4);
  return 0;
}
