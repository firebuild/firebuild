/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef COMMON_CSTRING_VIEW_H_
#define COMMON_CSTRING_VIEW_H_

/**
 * A lightweight structure containing a pointer to a string and the string's length.
 *
 * Intended to be used in a way that the string is a plain C '\0'-terminated string,
 * the length value doesn't include the trailing zero.
 *
 * Could be replaced by std::cstring_view, had this proposal not been rejected:
 *  - http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1402r0.pdf
 *  - https://github.com/cplusplus/papers/issues/189
 */

typedef struct {
  const char *c_str;
  uint32_t length;
} cstring_view;

#endif  // COMMON_CSTRING_VIEW_H_
