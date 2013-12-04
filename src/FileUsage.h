#ifndef FIREBUILD_FILEUSAGE_H
#define FIREBUILD_FILEUSAGE_H

#include <sys/stat.h>

#include "SHA256Hash.h"

using namespace std;

namespace firebuild 
{

class FileUsage
{
 public:
  /** Flags used when opening the file */
  int open_flags;
  /** Mode of opening the file only valid when flags include O_CREAT */
  mode_t mode;
  /** The file is did not exist before starting the process */
  bool created : 1;
  /** The file is deleted by the process */
  bool deleted : 1;
  /** The file is read by the process. It implies that the content of the file
   * may affect the process. */
  bool read : 1;
  /** The file is written by the process. It implies that the content of the
   * file may be changed by the process. */
  bool written : 1;
  /** An unhandled error occured during operation on the file. The process
   *  can't be short-cut, but the first such error code is stored here. */
  int unknown_err;
  SHA256Hash initial_hash;
  SHA256Hash final_hash;
  FileUsage (int flags, mode_t mode, bool c, bool d)
      :open_flags(flags), mode(mode), created(c), deleted(d), read(false),
      written(false), unknown_err(0), initial_hash(), final_hash() {};
};

}
#endif
