#ifndef FIREBUILD_FILE_H
#define FIREBUILD_FILE_H

#include <time.h>

#include <string>
#include <vector>

#include "SHA256Hash.h"

using namespace std;

namespace firebuild 
{

class File
{
 private:
  vector<timespec> mtimes;
  int update_hash();
 public:
  string path;
  bool exists;
  SHA256Hash hash;

  File (const string path);
  int update();
  int is_changed();
};

}
#endif
