/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

/**
 * string_array allows to conveniently build up an array of strings (i.e. NULL-terminated char**).
 */
typedef struct {
  char **p;
  int len;         /* excluding the trailing NULL */
  int size_alloc;  /* including the room for the trailing NULL */
} string_array;

void string_array_init(string_array *array);
void string_array_append(string_array *array, char *s);
void string_array_deep_free(string_array *array);

#endif /* FIREBUILD_UTILS_H_ */
