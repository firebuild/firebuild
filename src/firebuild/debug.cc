/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <time.h>
#include <sys/time.h>

#include "firebuild/debug.h"

namespace firebuild {

/**
 * Get a human friendly representation of a string, inside double
 * quotes, for debugging purposes.
 */
std::string d(const std::string& str, const int level) {
  (void)level;  /* unused */
  std::string ret = "\"";
  for (unsigned char c : str) {
    if (c < 0x20 || c >= 0x7f) {
      const char *hex = "0123456789ABCDEF";
      ret += "\\x";
      ret += hex[c / 16];
      ret += hex[c % 16];
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
 * Get a human friendly representation of a C string, inside double
 * quotes (unless NULL), for debugging purposes.
 */
std::string d(const char *str, const int level) {
  if (str) {
    return d(std::string(str), level);
  } else {
    return "NULL";
  }
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
std::string pretty_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t t = tv.tv_sec;
  struct tm local;
  localtime_r(&t, &local);
  int abs_diff_min = std::abs(local.tm_gmtoff) / 60;
  char buf[64];
  /* Note: strftime() doesn't support sub-seconds. */
  snprintf(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d.%06ld %c%02d%02d",
      1900 + local.tm_year, 1 + local.tm_mon, local.tm_mday,
      local.tm_hour, local.tm_min, local.tm_sec, tv.tv_usec,
      local.tm_gmtoff >= 0 ? '+' : '-', abs_diff_min / 60, abs_diff_min % 60);
  return std::string(buf);
}

struct flag {
  const char *name;
  int32_t value;
};

/* Keep this in sync with debug.h! */
static struct flag available_flags[] = {
  { "config",            FB_DEBUG_CONFIG },
  { "proc",              FB_DEBUG_PROC },
  { "proctree",          FB_DEBUG_PROCTREE },
  { "communication",     FB_DEBUG_COMM },
  { "comm",              FB_DEBUG_COMM },
  { "filesystem",        FB_DEBUG_FS },
  { "fs",                FB_DEBUG_FS },
  { "hash",              FB_DEBUG_HASH },
  { "cache",             FB_DEBUG_CACHE },
  { "caching",           FB_DEBUG_CACHING },
  { "shortcut",          FB_DEBUG_SHORTCUT },
  { "pipe",              FB_DEBUG_PIPE },
  { "function",          FB_DEBUG_FUNC },
  { "func",              FB_DEBUG_FUNC },
  { "time",              FB_DEBUG_TIME },
  { NULL, 0 }
};

#define SEPARATORS ",:"

/**
 * Parse the debug flags similarly to GLib's g_parse_debug_string().
 *
 * Currently case-sensitive (i.e. all lowercase is expected).
 */
int32_t parse_debug_flags(const std::string& str) {
  int32_t flags = 0;
  bool all = false;
  size_t pos = 0;

  while (pos < str.length()) {
    size_t start = str.find_first_not_of(SEPARATORS, pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = str.find_first_of(SEPARATORS, start);
    if (end == std::string::npos) {
      end = str.length();
    }
    std::string flag_str = str.substr(start, end - start);
    pos = end;

    bool found = false;
    if (flag_str == "all") {
      all = true;
      found = true;
    } else if (flag_str == "help") {
      fprintf(stderr, "Firebuild: available debug flags are:");
      int id = 0;
      while (available_flags[id].name != NULL) {
        if (id > 0 && available_flags[id].value == available_flags[id - 1].value) {
          fprintf(stderr, " or ");
        } else {
          fprintf(stderr, "\n  ");
        }
        fprintf(stderr, "%s", available_flags[id].name);
        id++;
      }
      fprintf(stderr, "\n  all\n");
      exit(EXIT_SUCCESS);
    } else {
      int id = 0;
      while (available_flags[id].name != NULL) {
        if (flag_str == available_flags[id].name) {
          flags |= available_flags[id].value;
          found = true;
          break;
        }
        id++;
      }
    }
    if (!found) {
      fprintf(stderr, "Firebuild: Unrecognized debug flag %s\n", flag_str.c_str());
    }
  }

  if (all) {
    flags ^= 0xFFFF;
  }
  return flags;
}

#ifdef FB_EXTRA_DEBUG
std::vector<int> fd_ages;
int method_tracker_level = 0;
#endif

}  // namespace firebuild
