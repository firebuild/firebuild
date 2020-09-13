/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "./fbb.h"

ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  /* send */

  const char *array[] = {"item1", "item02", "item003", NULL};

  int fd;
  if ((fd = creat("testing.bin", 0666)) < 0) {
    perror("open");
    exit(1);
  }

  FBB_Builder_testing testing_msgbldr;
  fbb_testing_init(&testing_msgbldr);

  fbb_testing_set_ri1(&testing_msgbldr, 42);
  fbb_testing_set_oi2(&testing_msgbldr, 100);
  fbb_testing_set_ri3(&testing_msgbldr, -200);

  fbb_testing_set_rs5(&testing_msgbldr, "foo");
  fbb_testing_set_os6(&testing_msgbldr, "loremipsum");
  fbb_testing_set_rs7(&testing_msgbldr, "quux");

  fbb_testing_set_sa9(&testing_msgbldr, (char * const *) array);
  fbb_testing_set_sa10(&testing_msgbldr, NULL);

  fbb_send(fd, &testing_msgbldr, 123 /* ack id */);

  close(fd);

  /* receive */

  if ((fd = open("testing.bin", O_RDONLY)) < 0) {
    perror("open");
    exit(1);
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    perror("fstat");
    exit(1);
  }
  if (st.st_size < 2 * (ssize_t) sizeof(uint32_t)) {
    fprintf(stderr, "message too short\n");
    exit(1);
  }
  int *p_orig, *p;
  p_orig = p = reinterpret_cast<int *>(mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0));
  if (p == NULL) {
    perror("mmap");
    exit(1);
  }

  if (*p++ != st.st_size - 2 * (ssize_t) sizeof(uint32_t)) {
    fprintf(stderr, "size mismatch\n");
    exit(1);
  }
  if (*p++ != 123) {
    fprintf(stderr, "ack id mismatch\n");
    exit(1);
  }

  FBB_testing *msg = reinterpret_cast<FBB_testing *>(p);

  if (!fbb_testing_has_oi2(msg)) {
    fprintf(stderr, "has_oi2 failed\n");
    exit(1);
  }
  if (fbb_testing_has_oi4(msg)) {
    fprintf(stderr, "!has_oi4 failed\n");
    exit(1);
  }
  if (!fbb_testing_has_os6(msg)) {
    fprintf(stderr, "has_os6 failed\n");
    exit(1);
  }
  if (fbb_testing_has_os8(msg)) {
    fprintf(stderr, "!has_os8 failed\n");
    exit(1);
  }

  if (fbb_testing_get_ri1(msg) != 42) {
    fprintf(stderr, "ri1 == 42 failed\n");
    exit(1);
  }
  if (fbb_testing_get_oi2(msg) != 100) {
    fprintf(stderr, "oi2 == 100 failed\n");
    exit(1);
  }
  if (fbb_testing_get_ri3(msg) != -200) {
    fprintf(stderr, "ri3 == -200 failed\n");
    exit(1);
  }
  if (strcmp(fbb_testing_get_rs5(msg), "foo") != 0) {
    fprintf(stderr, "rs5 == \"foo\" failed\n");
    exit(1);
  }
  if (strcmp(fbb_testing_get_os6(msg), "loremipsum") != 0) {
    fprintf(stderr, "os6 == \"loremipsum\" failed\n");
    exit(1);
  }
  if (strcmp(fbb_testing_get_rs7(msg), "quux") != 0) {
    fprintf(stderr, "rs7 == \"quux\" failed\n");
    exit(1);
  }

  int i = 0;
  const char* sa9[3];
  for_s_in_fbb_testing_sa9(msg, {sa9[i++] = s;});
  if (i != 3) {
    fprintf(stderr, "i == 3 failed\n");
  }
  if (strcmp(sa9[0], "item1") != 0) {
    fprintf(stderr, "sa9[0] == \"item1\" failed\n");
    exit(1);
  }
  if (strcmp(sa9[1], "item02") != 0) {
    fprintf(stderr, "sa9[1] == \"item02\" failed\n");
    exit(1);
  }
  if (strcmp(sa9[2], "item003") != 0) {
    fprintf(stderr, "sa9[2] == \"item003\" failed\n");
    exit(1);
  }

  std::vector<std::string> arr1 = fbb_testing_get_sa9(msg);
  if (arr1.size() != 3) {
    fprintf(stderr, "sa9.size() == 3 failed\n");
  }
  if (arr1[0] != "item1") {
    fprintf(stderr, "sa9[0] == \"item1\" failed\n");
    exit(1);
  }
  if (arr1[1] != "item02") {
    fprintf(stderr, "sa9[1] == \"item02\" failed\n");
    exit(1);
  }
  if (arr1[2] != "item003") {
    fprintf(stderr, "sa9[2] == \"item003\" failed\n");
    exit(1);
  }

  std::vector<std::string> arr2 = fbb_testing_get_sa10(msg);
  if (arr2.size() != 0) {
    fprintf(stderr, "sa10.size() == 0 failed\n");
    exit(1);
  }

  munmap(p_orig, st.st_size);
  close(fd);

  printf("fbb_test succeeded\n");

  return 0;
}
