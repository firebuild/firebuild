/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/*
 * Test for forking orphan children.
 */
#include <time.h>
#include <unistd.h>


int main() {
  /* Test not waiting, but orphaning a zombie child that quits early. */
  if (fork() > 0) {
    usleep(100*1000);
  } else {
    return 0;
  }

  /* Test not waiting, but orphaning a child that quits later. */
  if (fork() == 0) {
    usleep(100*1000);
    return 0;
  }

  /* Test forking an orphan that quits almost the same time as the parent. */
  fork();
  return 0;
}
