/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_DEBUG_H_
#define FIREBUILD_DEBUG_H_

#include <stdarg.h>
#include <string.h>

#include <string>
#include <vector>

namespace firebuild {

/** Print error message */
void fb_error(const std::string &msg);

/** Possible debug flags. Keep in sync with debug.cc! */
enum {
  /* Firebuild's configuration */
  FB_DEBUG_CONFIG       = 1 << 0,
  /* Events with one process, e.g. shortcut, exit */
  FB_DEBUG_PROC         = 1 << 1,
  /* How processes are organized into ProcTree */
  FB_DEBUG_PROCTREE     = 1 << 2,
  /* Communication */
  FB_DEBUG_COMM         = 1 << 3,
  /* File system */
  FB_DEBUG_FS           = 1 << 4,
  /* Checksum computation */
  FB_DEBUG_HASH         = 1 << 5,
  /* The data stored in the cache */
  FB_DEBUG_CACHE        = 1 << 6,
  /* Placing in / retrieving from the cache */
  FB_DEBUG_CACHING      = 1 << 7,
  /* Shortcutting */
  FB_DEBUG_SHORTCUT     = 1 << 8,
  /* Entering and leaving functions */
  FB_DEBUG_FUNC         = 1 << 10,
  /* Tracking the server-side file descriptors */
  FB_DEBUG_FD           = 1 << 11,
};


/**
 * Test if debugging this kind of events is enabled.
 */
#define FB_DEBUGGING(flag) ((firebuild::debug_flags) & flag)

/**
 * Print debug message if the given debug flag is enabled.
 */
#define FB_DEBUG(flag, msg) if (FB_DEBUGGING(flag)) \
    firebuild::fb_debug(msg)

void fb_debug(const std::string &msg);

/** Current debugging flags */
extern int32_t debug_flags;

int32_t parse_debug_flags(const std::string& str);

inline std::string d(int value) {
  return std::to_string(value);
}
inline std::string d(long value) {  /* NOLINT(runtime/int) */
  return std::to_string(value);
}
inline std::string d(long long value) {  /* NOLINT(runtime/int) */
  return std::to_string(value);
}
inline std::string d(unsigned int value) {
  return std::to_string(value);
}
inline std::string d(unsigned long value) {  /* NOLINT(runtime/int) */
  return std::to_string(value);
}
inline std::string d(unsigned long long value) {  /* NOLINT(runtime/int) */
  return std::to_string(value);
}

inline std::string d(bool value) {
  return value ? "true" : "false";
}

std::string d(const std::string& str);
std::string d(const char *str);

std::string d(const std::vector<std::string>& arr,
              const std::string& sep = ", ");

/* Convenience wrapper around our various d(...) debugging functions.
 * Instead of returning a std::string, as done by d(), this gives the raw C char* pointer
 * which is valid only inside the expression where D() is called. */
#define D(var) firebuild::d(var).c_str()

/** Get a human-readable timestamp according to local time. */
std::string pretty_timestamp();

#ifdef NDEBUG
#define TRACK(flag, fmt, ...)
#else
/* Track entering/leaving the function (or any brace-block of code)
 * if either "func" or any of the given flags is being debugged. */
#define TRACK(flag, fmt, ...) \
  firebuild::MethodTracker method_tracker(flag, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

class MethodTracker {
 public:
  MethodTracker(int flag, const char *func, const char *file, int line, const char *fmt, ...)
      __attribute__((format(printf, 6, 7)))
      : flag_(flag | FB_DEBUG_FUNC), func_(func) {
    if (FB_DEBUGGING(flag_)) {
      const char *last_slash = strrchr(file, '/');
      if (last_slash) {
        file = last_slash + 1;
      }
      char buf[1024];
      int run1 = snprintf(buf, sizeof(buf), "%*s-> %s()  [%s:%d]  ",
                          2 * level_, "", func, file, line);
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf + run1, sizeof(buf) - run1, fmt, ap);
      va_end(ap);
      FB_DEBUG(flag_, buf);
      level_++;
    }
  }
  ~MethodTracker() {
    if (FB_DEBUGGING(flag_)) {
      level_--;
      char buf[1024];
      snprintf(buf, sizeof(buf), "%*s<- %s()", 2 * level_, "", func_.c_str());
      FB_DEBUG(flag_, buf);
    }
  }

 private:
  int flag_;
  std::string func_;
  static int level_;
};
#endif  /* NDEBUG */

#ifndef NDEBUG
/*
 * Like an "assert(a op b)" statement, "assert_cmp(a, op, b)" makes sure that the "a op b" condition
 * is true. Note the required commas. Example: "assert_cmp(foo, >=, 0)".
 *
 * In case of failure, prints both values.
 *
 * Based on the idea of GLib's g_assert_cmp*(). With C++'s overloading we can do better, though.
 *
 * The two values can be of any type that's printable using d() and comparable, and accordingly,
 * they are indeed printed using d() if the comparison fails.
 *
 * Note: because d(NULL) doesn't work, you can't do "assert_cmp(p, ==, NULL)" or
 * "assert_cmp(p, !=, NULL)". For the former, use our "assert_null(p)". For the latter, use the
 * standard "assert(p)".
 */
#define assert_cmp(a, op, b) do { \
  if (!(a op b)) { \
    std::string source = #a " " #op " " #b; \
    std::string actual = firebuild::d(a) + " " + #op + " " + firebuild::d(b); \
    fprintf(stderr, "Assertion `%s': `%s' failed.\n", source.c_str(), actual.c_str()); \
    assert(0 && "see previous message"); \
  } \
} while (0)
/*
 * Like an assert(p == NULL), but if fails then prints the value using d().
 */
#define assert_null(p) do { \
  if (p != NULL) { \
    std::string source = #p " != NULL"; \
    std::string actual = firebuild::d(p) + " != NULL"; \
    fprintf(stderr, "Assertion `%s': `%s' failed.\n", source.c_str(), actual.c_str()); \
    assert(0 && "see previous message"); \
  } \
} while (0)
#else
#define assert_cmp(a, op, b)
#define assert_null(p)
#endif  /* NDEBUG */

}  // namespace firebuild
#endif  // FIREBUILD_DEBUG_H_
