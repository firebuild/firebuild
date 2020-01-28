/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "debug.h"

#include <time.h>
#include <sys/time.h>

namespace firebuild {

/**
 * Get a human friendly representation of a string, inside double
 * quotes, for debugging purposes.
 */
std::string pretty_print_string(const std::string& str) {
  std::string ret = "\"";
  for (unsigned char c : str) {
    if (c < 0x20 || c >= 0x7f) {
      ret += "\\";
      // NOTE: Protobuf uses octal, but do we also want to?
      ret += ('0' + (c / 64));
      ret += ('0' + (c / 8 % 8));
      ret += ('0' + (c % 8));
    } else if (c == '\\' || c == '"') {
      ret += "\\";
      ret += c;
    } else {
      ret += c;
    }
  }
  ret += "\"";
  return ret;
}

/**
 * Get a human friendly representation of an array of strings, like:
 *
 *   ["item1", "item2", "item3"]
 *
 * for debugging purposes.
 *
 * A custom separator instead of the default ", " can be specified, in
 * order to print each item in a separate line. In this case the caller
 * should put the required number of spaces in the separator, to achieve
 * the desired indentation level.
 */
std::string pretty_print_array(const std::vector<std::string>& arr,
                               const std::string& sep) {
  std::string res = "[";
  bool add_sep = false;
  for (const auto &val : arr) {
    if (add_sep) {
      res += sep;
    }
    res += pretty_print_string(val);
    add_sep = true;
  }
  res += "]";
  return res;
}

/**
 * Get a human friendly representation of the current local time, for
 * debugging purposes.
 *
 * The format was chosen as a compromise between standards, common
 * practices, best readability, and best accuracy. It currently looks
 * like:
 *
 *   2019-12-31 23:59:59.999999 +0100
 */
std::string pretty_print_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t t = tv.tv_sec;
  struct tm *local = localtime(&t);
  int abs_diff_min = abs(local->tm_gmtoff) / 60;
  char buf[64];
  /* Note: strftime() doesn't support sub-seconds. */
  sprintf(buf, "%d-%02d-%02d %02d:%02d:%02d.%06ld %c%02d%02d",
      1900 + local->tm_year, 1 + local->tm_mon, local->tm_mday,
      local->tm_hour, local->tm_min, local->tm_sec, tv.tv_usec,
      local->tm_gmtoff >= 0 ? '+' : '-', abs_diff_min / 60, abs_diff_min % 60);
  return std::string(buf);
}

}  // namespace firebuild
