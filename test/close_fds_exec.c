/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <unistd.h>

int main(int argc, char* argv[]) {
  (void) argc;  /* unused */

  int i;
  for (i = 3; i < 120; i++) {
    close(i);
  }
  execvp(argv[1], &argv[1]);
}
