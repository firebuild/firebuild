/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "firebuild/debug.h"

#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "common/debug_sysflags.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/options.h"
#include "firebuild/process.h"

namespace firebuild {

ExeMatcher* debug_filter {nullptr};
__thread bool debug_suppressed {false};
int32_t debug_flags = 0;

void fb_error(const std::string &msg) {
  fprintf(stderr, "FIREBUILD ERROR: %s\n", msg.c_str());
}

void fb_info(const std::string &msg) {
  if (!Options::quiet()) {
    fprintf(stdout, "FIREBUILD: %s\n", msg.c_str());
  }
}

void fb_debug(const std::string &msg) {
  if (!debug_suppressed) {
    fprintf(stderr, "FIREBUILD: %s\n", msg.c_str());
  }
}

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

std::string d(const char *str, const int level) {
  if (str) {
    return d(std::string(str), level);
  } else {
    return "NULL";
  }
}

std::string d(const struct stat64& st, const int level) {
  (void)level;  /* unused */

  char *modestring_ptr = NULL;
  size_t modestring_sizeloc = -1;
  FILE *f = open_memstream(&modestring_ptr, &modestring_sizeloc);
  debug_mode_t(f, st.st_mode);
  fclose(f);

  std::string ret = std::string("{stat mode=") + modestring_ptr + " size=" + d(st.st_size) + "}";
  free(modestring_ptr);
  return ret;
}

std::string d(const struct stat64 *st, const int level) {
  if (st) {
    return d(*st, level);
  } else {
    return "{stat NULL}";
  }
}

std::string pretty_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t t = tv.tv_sec;
  struct tm local;
  localtime_r(&t, &local);
  int abs_diff_min = std::abs(local.tm_gmtoff) / 60;
  char buf[64];
  /* Note: strftime() doesn't support sub-seconds. */
#ifdef __APPLE__
  snprintf(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d.%06d %c%02d%02d",
#else
  snprintf(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d.%06ld %c%02d%02d",
#endif
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
  { "deterministic-cache",
                         FB_DEBUG_DETERMINISTIC_CACHE },
  { "caching",           FB_DEBUG_CACHING },
  { "shortcut",          FB_DEBUG_SHORTCUT },
  { "pipe",              FB_DEBUG_PIPE },
  { "function",          FB_DEBUG_FUNC },
  { "func",              FB_DEBUG_FUNC },
  { "time",              FB_DEBUG_TIME },
  { NULL, 0 }
};

#define SEPARATORS ",:"

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

void init_debug_filter(const std::string commands) {
  /* Allow passing debug filter to the firebuild binary multiple times. */
  if (debug_filter) {
    delete(debug_filter);
  }
  debug_filter = new firebuild::ExeMatcher();

  size_t pos = 0;
  while (pos < commands.length()) {
    size_t start = commands.find_first_not_of(",", pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = commands.find_first_of(",", start);
    if (end == std::string::npos) {
      end = commands.length();
    }
    debug_filter->add(commands.substr(start, end - start));
    pos = end;
  }
}

#ifdef FB_EXTRA_DEBUG
std::vector<int> fd_ages;
int method_tracker_level = 0;
#endif

}  /* namespace firebuild */
