/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <unistd.h>

int main(int argc, char* argv[]) {
  int i;
  for (i = 3; i < 120; i++) {
    close(i);
  }
  execvp(argv[1], &argv[1]);
}
