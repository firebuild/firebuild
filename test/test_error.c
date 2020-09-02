/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

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
