/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <unistd.h>

int main() {
  char * const argv[] = {"echo", "ok", NULL};
  execvp("foo", argv);
  execvp("echo", argv);
  return 0;
}
