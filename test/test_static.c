/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

/* This binary is meant to be statically linked. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  puts("I am statically linked.");
  fflush(stdout);
  if (argc > 1) {
    int recurse_level = 0;
    sscanf(argv[1], "%d", &recurse_level);
    while (recurse_level-- > 0) {
      pid_t child_pid = fork();
      if (child_pid > 0) {
        wait(NULL);
        return 0;
      }
    }
    return system("echo end");
  }
  return 0;
}
