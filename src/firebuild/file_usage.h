/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_USAGE_H_
#define FIREBUILD_FILE_USAGE_H_

#include <sys/stat.h>

#include <string>

#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

typedef enum {
  /** We don't have any information about this file yet (used only temporarily
   *  while building up our data structures). */
  DONTKNOW,
  /** We know and care that the file or directory did not exist before (e.g. an
   *  open(O_CREAT|O_WRONLY|O_EXCL) or a mkdir() succeeded). */
  NOTEXIST,
  /** We know and care that the file either did not exist before, or was
   *  a zero sized regular file (but we do not know which of these, because
   *  an open(O_CREAT|O_WRONLY) succeeded and resulted in a zero length file). */
  NOTEXIST_OR_ISREG_EMPTY,
  /** We know and care that the file either did not exist before, or was a
   *  regular file (e.g. a creat() a.k.a. open(O_CREAT|O_WRONLY|O_TRUNC) succeeded). */
  NOTEXIST_OR_ISREG,
  /** We know and care that the regular file existed before, but don't care
   *  about its previous contents (e.g. an open(O_WRONLY|O_TRUNC) succeeded). */
  ISREG,
  /** We know and care that the file existed with the given hash (we opened it
   *  for reading). */
  ISREG_WITH_HASH,
  /** We know and care that the directory existed before, but don't care about
   *  its previous listing (e.g. we successfully created a file underneath it). */
  ISDIR,
  /** We know and care that the directory existed with the given file listing hash
   *  (e.g. an opendir() was performed. */
  ISDIR_WITH_HASH,
} FileInitialState;

typedef enum {
  /** Performed an open() or creat() on the path */
  FILE_ACTION_OPEN,
  /** Performed a mkdir() on the path */
  FILE_ACTION_MKDIR,
} FileAction;

class FileUsage {
 public:
  FileUsage(FileInitialState initial_state, Hash hash) :
      initial_state_(initial_state),
      /*read_(false),*/ initial_hash_(hash),
      stated_(false), initial_stat_(), initial_stat_err_(0),
      written_(false), stat_changed_(true), unknown_err_(0) {}
  explicit FileUsage(FileInitialState initial_state) :
      FileUsage(initial_state, Hash()) {}
  FileUsage() : FileUsage(DONTKNOW) {}

  FileInitialState initial_state() const {return initial_state_;}
  const Hash& initial_hash() const {return initial_hash_;}
  bool written() const {return written_;}
  void set_written(bool val) {written_ = val;}

  int unknown_err() {return unknown_err_;}
  void set_unknown_err(int e) {unknown_err_ = e;}

  bool merge(const FileUsage& fu);
  bool update_from_open_params(const FileName* filename,
                               FileAction action, int flags, int err,
                               bool do_read);
  bool open(const FileName* filename, int flags, int err, Hash **hashpp);


 private:
  /* Things that describe the filesystem when the process started up */

  /** The file's contents at the process's startup. More precisely, at
   *  the time the process first tries to do anything with this file
   *  that could be relevant, because at process startup we have no idea
   *  which files we'll need to monitor. See the comment of the
   *  individual enum numbers for more details. */
  FileInitialState initial_state_;

  /** The initial checksum, if initial_state_ == EXIST_WITH_HASH. */
  Hash initial_hash_;

  /** If the file was stat()'ed during the process's lifetime, that is,
   *  its initial metadata might be relevant. */
  bool stated_ : 1;

  /** The result of initially stat()'ing the file. Only valid if stated_
   *  && !initial_stat_err_. */
  struct stat64 initial_stat_;

  /** The error from initially stat()'ing the file, or 0 if there was no
   *  error. */
  int initial_stat_err_;


  /* Things that describe what the process potentially did */

  /** The file's contents were altered by the process, e.g. written to,
   *  or modified in any other way, including removal of the file, or
   *  another file getting renamed to this one. */
  bool written_ : 1;

  /** If the file's metadata (e.g. mode) was potentially altered, that is,
   *  the final state is to be remembered.
   *  FIXME Do we need this? We should just always stat() at the end. */
  bool stat_changed_ : 1;

  /* Note: stuff like the final hash are not stored here. They are
   * computed right before being placed in the cache, don't need to be
   * remembered in memory. */


  /* Misc */

  /** An unhandled error occured during operation on the file. The process
   *  can't be short-cut, but the first such error code is stored here. */
  int unknown_err_;
};

struct file_file_usage {
  const FileName* file;
  const FileUsage* usage;
};

bool file_file_usage_cmp(const file_file_usage& lhs, const file_file_usage& rhs);

}  // namespace firebuild
#endif  // FIREBUILD_FILE_USAGE_H_
