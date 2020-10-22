/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* Performs a fork(), execvp() in the child, waitpid() in the parent.
 * The command to execute and its parameters are taken from the command line, one by one.
 * Returns 0 (unless an error occurred), not the command's exit code. */

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "need at least 1 argument\n");
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    /* child */
    execvp(argv[1], argv + 1);

    perror("execvp");
    return 1;
  } else {
    /* parent */
    if (waitpid(pid, NULL, 0) < 0) {
      perror("waitpid");
      return 1;
    }

    return 0;
  }
}
