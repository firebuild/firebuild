#ifndef FIREBUILD_FILE_H
#define FIREBUILD_FILE_H

#include <time.h>

#include <string>
#include <vector>

#include "SHA256Hash.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{

class File
{
 public:
  std::string path;
  bool exists;
  SHA256Hash hash;

  explicit File (const std::string path);
  int update();
  int is_changed();
 private:
  std::vector<timespec> mtimes;
  int update_hash();
  DISALLOW_COPY_AND_ASSIGN(File);
};

}
#endif
