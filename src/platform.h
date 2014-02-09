#ifndef FIREBUILD_PLATFORM_H
#define FIREBUILD_PLATFORM_H

#include <cassert>
#include <string>

#define FB_MISSING(thing) assert(0 && "Missing" && thing)

namespace firebuild {

#ifdef __clang__
extern "C" {
extern void* __builtin_apply_args() {
  FB_MISSING(__func__);
  return NULL;
}

extern void* __builtin_apply(void (*)(...), void *, size_t) {
  FB_MISSING(__func__);
  return NULL;
}
}

#endif

namespace platform {

bool path_is_absolute(const std::string &p) {
#ifdef _WIN32
  return !PathIsRelative(p);
#else
  if ((p.length() >= 1) && (p.at(0) == '/')) {
    return true;
    } else {
    return false;
  }
#endif
}

}  // namespace platform
}  // namespace firebuild
#endif
