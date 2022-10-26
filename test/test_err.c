/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void atexit_handler() {
  fprintf(stderr, "atexit_handler\n");
}

int main() {
  atexit(atexit_handler);

  errno = ENOENT;
  warn("warn%d", 1);
  errno = EACCES;
  warn("warn%d", 2);
  errno = ENOENT;
  err(1, "err%d", 1);
  /* should not reach here */
  errno = EACCES;
  err(1, "err%d", 2);
  return 0;
}
