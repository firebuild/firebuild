
#include "FileDB.h"

namespace firebuild {

FileDB::~FileDB() {
  for (auto it = this->begin(); it != this->end(); ++it) {
    delete(it->second);
  }
}

}  // namespace firebuild
