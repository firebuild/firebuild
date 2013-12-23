#ifndef FIREBUILD_FILEDB_H
#define FIREBUILD_FILEDB_H

#include <string>
#include <unordered_map>

#include "File.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{
  class FileDB: public std::unordered_map<std::string, File*>
{
 public:
  static FileDB* getInstance()
  {
    static FileDB    instance;   // Guaranteed to be destroyed.
    // Instantiated on first use.
    return &instance;
  }
 private:
  FileDB() {};
  DISALLOW_COPY_AND_ASSIGN(FileDB);
  ~FileDB();
};

}
#endif
