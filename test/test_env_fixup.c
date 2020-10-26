/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

extern char **environ;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  char *myenv[] = {
    "AAA=aaa",
    "LD_PRELOAD=  LIBXXX.SO  libfbintercept.so  LIBYYY.SO  ",
    NULL
  };
  environ = myenv;
  setenv("BBB", "bbb", 0);

  return system("printenv");
}
