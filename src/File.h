#ifndef FIREBUILD_FILE_H
#define FIREBUILD_FILE_H

#include <time.h>

#include <string>
#include <vector>

#include "SHA256Hash.h"

namespace firebuild 
{

class File
{
 private:
  std::vector<timespec> mtimes;
  int update_hash();
 public:
  std::string path;
  bool exists;
  SHA256Hash hash;

  File (const std::string path);
  int update();
  int is_changed();
};

}
#endif
