/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/random.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main() {
  char buf[4];
  ssize_t ret_len = getrandom(buf, sizeof(buf), 0);
  if (ret_len == -1) {
    perror("getrandom" LOC);
    exit(1);
  }
  assert(ret_len == sizeof(buf));

  ret_len = getrandom(buf, sizeof(buf), GRND_RANDOM);
  if (ret_len == -1) {
    perror("getrandom" LOC);
    exit(1);
  }
  assert(ret_len <= (ssize_t) sizeof(buf));  /* With GRND_RANDOM, short read is possible. */

  return 0;
}
