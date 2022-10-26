/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>

/* Performs a posix_spawnp() / waitpid() pair.
 * The command to execute and its parameters are taken from the command line, one by one.
 * Returns 0 (unless an error occurred), not the command's exit code. */

extern char **environ;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "need at least 1 argument\n");
    return 1;
  }

  pid_t pid;
  int ret = posix_spawnp(&pid, argv[1], NULL, NULL, argv + 1, environ);
  if (ret) {
    errno = ret;
    perror("posix_spawnp");
    return 1;
  }

  if (waitpid(pid, NULL, 0) < 0) {
    perror("waitpid");
    return 1;
  }

  return 0;
}
