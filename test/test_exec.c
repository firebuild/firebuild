/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <unistd.h>

int main() {
  char * const argv[] = {"echo", "ok", NULL};
  execvp("foo", argv);
  execvp("echo", argv);
  return 0;
}
