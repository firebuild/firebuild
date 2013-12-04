
#include "FileDB.h"

using namespace std;
namespace firebuild {

FileDB::~FileDB()
 {
   for (auto it = this->begin(); it != this->end(); ++it) {
     delete(it->second);
   }
 }

}
