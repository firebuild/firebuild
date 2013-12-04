
#include "FileUsage.h"

using namespace std;
namespace firebuild {

FileUsage::FileUsage (File *f, int fd, mode_t m) {
  this->file = f;
  this->mode = m;
  this->fd = fd;
}

}
