#ifndef FIREBUILD_FILE_H
#define FIREBUILD_FILE_H

#include <time.h>

#include <string>
#include <vector>

#include "SHA256Hash.h"
#include "cxx_lang_utils.h"

namespace firebuild {

class File {
 public:
  explicit File(const std::string &path);
  int update();
  int is_changed();
  std::string& path() {return path_;}
  SHA256Hash& hash() {return hash_;}

 private:
  std::vector<timespec> mtimes_;
  std::string path_;
  bool exists_;
  SHA256Hash hash_;
  int update_hash();
  DISALLOW_COPY_AND_ASSIGN(File);
};

}  // namespace firebuild
#endif
