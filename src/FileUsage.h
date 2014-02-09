#ifndef FIREBUILD_FILEUSAGE_H
#define FIREBUILD_FILEUSAGE_H

#include <sys/stat.h>

#include "SHA256Hash.h"
#include "cxx_lang_utils.h"

namespace firebuild {

class FileUsage {
 public:
  FileUsage(int flags, mode_t mode, bool c, bool d, bool open_failed, int err)
      :open_flags_(flags), mode_(mode), created_(c), deleted_(d), read_(false),
      written_(false), open_failed_(open_failed), err_(err), unknown_err_(0),
      initial_hash_(), final_hash_() {}
  int open_flags() {return open_flags_;}
  bool created() {return created_;}
  bool open_failed() {return open_failed_;}
  int unknown_err() {return unknown_err_;}
  void set_unknown_err(int e) {unknown_err_ = e;}
  void set_initial_hash(const SHA256Hash &h) {initial_hash_ = h;}

 private:
  /** Flags used when opening the file */
  int open_flags_;
  /** Mode of opening the file only valid when flags include O_CREAT */
  mode_t mode_;
  /** The file is did not exist before starting the process */
  bool created_ : 1;
  /** The file is deleted by the process */
  bool deleted_ : 1;
  /** The file is read by the process. It implies that the content of the file
   * may affect the process. */
  bool read_ : 1;
  /** The file is written by the process. It implies that the content of the
   * file may be changed by the process. */
  bool written_ : 1;
  /** The file could not be opened by the process */
  bool open_failed_ : 1;
  /** error code for failed open */
  int err_;
  /** An unhandled error occured during operation on the file. The process
   *  can't be short-cut, but the first such error code is stored here. */
  int unknown_err_;
  SHA256Hash initial_hash_;
  SHA256Hash final_hash_;
  DISALLOW_COPY_AND_ASSIGN(FileUsage);
};

}  // namespace firebuild
#endif
