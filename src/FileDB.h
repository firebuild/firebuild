#ifndef FIREBUILD_FILEDB_H
#define FIREBUILD_FILEDB_H

#include <string>
#include <unordered_map>

#include "File.h"

using namespace std;

namespace firebuild 
{
  class FileDB: public unordered_map<string, File*>
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
  FileDB(FileDB const&);         // Don't Implement
  void operator=(FileDB const&); // Don't implement
  ~FileDB();
};

}
#endif
