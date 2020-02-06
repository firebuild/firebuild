/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILEUSAGE_H_
#define FIREBUILD_FILEUSAGE_H_

#include <sys/stat.h>

#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

typedef enum {
  /** We don't care if the file existed before or not, and what its
   *  contents were. */
  DONTCARE,
  /** We know and care that the file did not exist before. */
  NOTEXIST,
  /** We know and care that the file either did not exist before, or was
   *  zero sized (but we do not know which of these). */
  NOTEXIST_OR_EMPTY,
  /** We know and care that the file existed before, but don't care
   *  about its previous contents. */
  EXIST,
  /** We know and care that the file existed with the given hash. */
  EXIST_WITH_HASH,
} FileInitialState;

class FileUsage {
 public:
  FileUsage() :
      initial_state_(DONTCARE),
      /*read_(false),*/ initial_hash_(),
      stated_(false), initial_stat_(), initial_stat_err_(0),
      written_(false), stat_changed_(true), unknown_err_(0) {}

  FileInitialState initial_state() const {return initial_state_;}
  const Hash& initial_hash() const {return initial_hash_;}
  bool written() const {return written_;}

  int unknown_err() {return unknown_err_;}
  void set_unknown_err(int e) {unknown_err_ = e;}

  void merge(const FileUsage& fu);
  bool update_from_open_params(const std::string& filename, int flags, int err,
                               bool do_read);
  bool open(const std::string& filename, int flags, int err, Hash **hashpp);


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


  DISALLOW_COPY_AND_ASSIGN(FileUsage);
};

}  // namespace firebuild
#endif  // FIREBUILD_FILEUSAGE_H_
