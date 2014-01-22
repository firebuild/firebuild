#ifndef FIREBUILD_PLATFORM_H
#define FIREBUILD_PLATFORM_H

#include <string>

namespace firebuild
{
namespace platform
{

bool path_is_absolute (const std::string &p)
{
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

}
}
#endif
