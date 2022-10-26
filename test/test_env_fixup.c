/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

extern char **environ;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  char *myenv[] = {
    "AAA=aaa",
    "LD_PRELOAD=  LIBXXX.SO  libfirebuild.so  LIBYYY.SO  ",
    NULL
  };
  environ = myenv;
  setenv("BBB", "bbb", 0);

  return system("printenv");
}
