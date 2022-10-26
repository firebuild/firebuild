/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

/* Cancel the gcc command line "-DNDEBUG", if present,
 * so that we can use assert() even with prod builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "./fbbtest.h"

const char *my_stringarray_item_fn(int index, const void *user_data, fbb_size_t *len_out) {
  if (user_data != (void *) 42) {
    fprintf(stderr, "user_data mismatch\n");
    exit(1);
  }

  if (index == 0) {
    if (len_out) {
      *len_out = 3;
    }
    return "one";
  } else if (index == 1) {
    if (len_out) {
      *len_out = 3;
    }
    return "two";
  } else if (index == 2) {
    if (len_out) {
      *len_out = 5;
    }
    return "three";
  } else {
    if (len_out) {
      *len_out = 4;
    }
    return "four";
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  /* construct the builder */

  const int int_array[] = { 33 };
  /* 8 bytes, so it won't get a padding i.e. a trailing '\0' */
  const char char_array[] = { 'W', 'e', 'l', 'c', 'o', 'm', 'e', '!' };
  const char *string_array[] = { "lorem1",    /* trailing '\0' plus 1 byte padding */
                                 "lorem02",   /* trailing '\0' and no padding */
                                 "lorem003",  /* trailing '\0' plus 3 or 7 bytes of padding */
                                 NULL };
  const char *string_array2[] = { "the", "quick", "brown", "fox", NULL };

  FBBTEST_Builder_testing builder;
  fbbtest_builder_testing_init(&builder);

  fbbtest_builder_testing_set_reqint(&builder, 42);
  fbbtest_builder_testing_set_optint(&builder, 100);
  fbbtest_builder_testing_set_arrint(&builder, int_array, 1 /* length */);

  fbbtest_builder_testing_set_reqchr(&builder, 'x');
  /* leaving optchr unset */
  fbbtest_builder_testing_set_arrchr(&builder, char_array, 8 /* length */);

  fbbtest_builder_testing_set_reqstr(&builder, "foo");
  fbbtest_builder_testing_set_optstr(&builder, "quux");
  fbbtest_builder_testing_set_arrstr(&builder, string_array);
  fbbtest_builder_testing_set_arrstr2(&builder, string_array2);

  FBBTEST_Builder_testing2 builder2;
  fbbtest_builder_testing2_init(&builder2);

  fbbtest_builder_testing2_set_t2(&builder2, 60);
  fbbtest_builder_testing_set_reqfbb(&builder, reinterpret_cast<FBBTEST_Builder *>(&builder2));

  FBBTEST_Builder_testing3 builder3;
  fbbtest_builder_testing3_init(&builder3);

  fbbtest_builder_testing_set_optfbb(&builder, reinterpret_cast<FBBTEST_Builder *>(&builder3));

  FBBTEST_Builder_testing builder4;
  fbbtest_builder_testing_init(&builder4);
  fbbtest_builder_testing_set_reqint(&builder4, 44);
  fbbtest_builder_testing_set_reqchr(&builder4, 'y');
  fbbtest_builder_testing_set_reqstr(&builder4, "hi there");
  fbbtest_builder_testing_set_arrstr_item_fn(&builder4, 4, my_stringarray_item_fn,
                                             reinterpret_cast<void *>(42));
  fbbtest_builder_testing_set_reqfbb(&builder4, reinterpret_cast<FBBTEST_Builder *>(&builder2));

  FBBTEST_Builder_testing2 builder5;
  fbbtest_builder_testing2_init(&builder5);
  fbbtest_builder_testing2_set_t2(&builder5, 70);

  FBBTEST_Builder_testing2 builder6;
  fbbtest_builder_testing2_init(&builder6);

  FBBTEST_Builder **builder_array =
      reinterpret_cast<FBBTEST_Builder **>(malloc(4 * sizeof(FBBTEST_Builder *)));
  builder_array[0] = reinterpret_cast<FBBTEST_Builder *>(&builder4);
  builder_array[1] = reinterpret_cast<FBBTEST_Builder *>(&builder5);
  builder_array[2] = reinterpret_cast<FBBTEST_Builder *>(&builder6);
  builder_array[3] = NULL;
  fbbtest_builder_testing_set_arrfbb(&builder, builder_array);

  /* debug the builder to a file */

  FILE *builder_debug_f = fopen("fbb_test_builder_debug.txt", "w");
  assert(builder_debug_f != NULL);
  fbbtest_builder_debug(builder_debug_f, reinterpret_cast<const FBBTEST_Builder *>(&builder));
  fclose(builder_debug_f);

  int builder_debug_fd = open("fbb_test_builder_debug.txt", O_RDONLY);
  assert(builder_debug_fd >= 0);

  struct stat builder_debug_st;
  assert(fstat(builder_debug_fd, &builder_debug_st) == 0);
  void *builder_debug_p = mmap(NULL, builder_debug_st.st_size, PROT_READ, MAP_SHARED,
                               builder_debug_fd, 0);
  assert(builder_debug_p != MAP_FAILED);

  /* serialize to memory */

  size_t len = fbbtest_builder_measure(reinterpret_cast<FBBTEST_Builder *>(&builder));
  char *p = reinterpret_cast<char *>(malloc(len));
  assert(fbbtest_builder_serialize(reinterpret_cast<FBBTEST_Builder *>(&builder), p) == len);

  /* dump to file for easier debugging */

  int fd = open("fbb_test.bin", O_CREAT|O_RDWR|O_TRUNC, 0666);
  assert(fd >= 0);
  assert(write(fd, p, len) == static_cast<ssize_t>(len));
  close(fd);

  /* debug the serialized version to a file */

  FILE *serialized_debug_f = fopen("fbb_test_serialized_debug.txt", "w");
  assert(serialized_debug_f != NULL);
  fbbtest_serialized_debug(serialized_debug_f, reinterpret_cast<const FBBTEST_Serialized *>(p));
  fclose(serialized_debug_f);

  int serialized_debug_fd = open("fbb_test_serialized_debug.txt", O_RDONLY);
  assert(serialized_debug_fd >= 0);
  struct stat serialized_debug_st;
  assert(fstat(serialized_debug_fd, &serialized_debug_st) == 0);
  void *serialized_debug_p = mmap(NULL, serialized_debug_st.st_size, PROT_READ, MAP_SHARED,
                                  serialized_debug_fd, 0);
  assert(serialized_debug_p != MAP_FAILED);

  /* compare the two debug outputs */

  assert(builder_debug_st.st_size == serialized_debug_st.st_size);
  assert(memcmp(builder_debug_p, serialized_debug_p, builder_debug_st.st_size) == 0);

  /* check the serialized version's fields manually */

  assert(fbbtest_serialized_get_tag(reinterpret_cast<FBBTEST_Serialized *>(p)) ==
         FBBTEST_TAG_testing);
  FBBTEST_Serialized_testing *msg = reinterpret_cast<FBBTEST_Serialized_testing *>(p);

  /* check if optionals are set */
  assert(fbbtest_serialized_testing_has_optint(msg));
  assert(!fbbtest_serialized_testing_has_optchr(msg));
  assert(fbbtest_serialized_testing_has_optstr(msg));
  assert(fbbtest_serialized_testing_has_optfbb(msg));

  /* check the values of required and optional scalars */
  assert(fbbtest_serialized_testing_get_reqint(msg) == 42);
  assert(fbbtest_serialized_testing_get_optint(msg) == 100);
  assert(fbbtest_serialized_testing_get_reqchr(msg) == 'x');

  assert(fbbtest_serialized_testing_get_reqstr_len(msg) == 3);
  assert(fbbtest_serialized_testing_get_optstr_len(msg) == 4);
  assert(strcmp(fbbtest_serialized_testing_get_reqstr(msg), "foo") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_optstr(msg), "quux") == 0);

  /* check the values of arrays, C api */
  assert(fbbtest_serialized_testing_get_arrint_count(msg) == 1);
  assert(fbbtest_serialized_testing_get_arrint_at(msg, 0) == 33);

  assert(fbbtest_serialized_testing_get_arrchr_count(msg) == 8);
  assert(strncmp(fbbtest_serialized_testing_get_arrchr(msg), "Welcome!", 8) == 0);

  assert(fbbtest_serialized_testing_get_arrstr_count(msg) == 3);
  assert(fbbtest_serialized_testing_get_arrstr_len_at(msg, 0) == 6);
  assert(fbbtest_serialized_testing_get_arrstr_len_at(msg, 1) == 7);
  assert(fbbtest_serialized_testing_get_arrstr_len_at(msg, 2) == 8);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(msg, 0), "lorem1") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(msg, 1), "lorem02") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(msg, 2), "lorem003") == 0);

  /* check the values of arrays, C++ api */
  std::vector<int> cxx_ints = fbbtest_serialized_testing_get_arrint_as_vector(msg);
  assert(cxx_ints.size() == 1);
  assert(cxx_ints[0] == 33);

  std::vector<char> cxx_chars = fbbtest_serialized_testing_get_arrchr_as_vector(msg);
  assert(cxx_chars.size() == 8);
  assert(std::string(cxx_chars.begin(), cxx_chars.end()) == "Welcome!");

  std::vector<std::string> cxx_strings = fbbtest_serialized_testing_get_arrstr_as_vector(msg);
  assert(cxx_strings.size() == 3);
  assert(cxx_strings[0] == "lorem1");
  assert(cxx_strings[1] == "lorem02");
  assert(cxx_strings[2] == "lorem003");

  /* check some of the embedded FBBs */
  assert(fbbtest_serialized_testing_get_arrfbb_count(msg) == 3);
  const FBBTEST_Serialized *fbb0_generic = fbbtest_serialized_testing_get_arrfbb_at(msg, 0);
  assert(fbbtest_serialized_get_tag(fbb0_generic) == FBBTEST_TAG_testing);
  const FBBTEST_Serialized_testing *fbb0 =
      reinterpret_cast<const FBBTEST_Serialized_testing *>(fbb0_generic);
  assert(fbbtest_serialized_testing_get_arrstr_count(fbb0) == 4);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(fbb0, 0), "one") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(fbb0, 1), "two") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(fbb0, 2), "three") == 0);
  assert(strcmp(fbbtest_serialized_testing_get_arrstr_at(fbb0, 3), "four") == 0);

  printf("fbb testing succeeded\n");
  return 0;
}
