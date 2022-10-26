/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

/* Based on clone(2) man page. */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)    /* Stack size for cloned child */

/* Performs a clone() / waitpid() pair.
 * The command executes and its parameter taken from the command line.
 * Returns 0 (unless an error occurred), not the command's exit code. */

int child(void *arg) {
  return execv(*(char**)arg, (char**)arg);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "need at least 1 argument\n");
    return 1;
  }

  char *stack, *stack_top;
  stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
               -1, 0);
  if (stack == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  stack_top = stack + STACK_SIZE;
  int ret = clone(child, stack_top, CLONE_PTRACE|SIGCHLD, &argv[1]);
  if (ret == -1) {
    errno = ret;
    perror("clone");
    return 1;
  }

  if (waitpid(ret, NULL, 0) < 0) {
    perror("waitpid");
    return 1;
  }

  return 0;
}
