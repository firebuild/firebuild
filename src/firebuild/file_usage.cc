/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * FileUsage describes, for one particular Process and one particular
 * filename, the inital and final contents found at the given location
 * with as much accuracy as it matters to us.
 *
 * E.g. if the Process potentially reads from the file then its original
 * hash is computed and stored here, but if the Process does not read
 * the contents then it is not stored. Similarly, it's recorded whether
 * the process potentially modified the file.
 */

#include "firebuild/file_usage.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "firebuild/hash.h"

static inline bool is_rdonly(int flags) { return ((flags & O_ACCMODE) == O_RDONLY); }
static inline bool is_wronly(int flags) { return ((flags & O_ACCMODE) == O_WRONLY); }
static inline bool is_rdwr(int flags)   { return ((flags & O_ACCMODE) == O_RDWR); }
static inline bool is_read(int flags)   { return (is_rdonly(flags) || is_rdwr(flags)); }
static inline bool is_write(int flags)  { return (is_wronly(flags) || is_rdwr(flags)); }

namespace firebuild {

/**
 * Merge the other FileUsage object into this one.
 *
 * "this" describes the older event(s) which happened to a file, and
 * "that" describes the new one. "this" is updated to represent what
 * happened to the file so far.
 */
void FileUsage::merge(const FileUsage& that) {
  if (initial_state_ == DONTCARE) {
    initial_state_ = that.initial_state_;
    if (that.initial_state_ == EXIST_WITH_HASH) {
      initial_hash_ = that.initial_hash_;
    }
  }
  written_ = written_ || that.written_;
}

/**
 * Based on the parameters and return value of an open() or similar
 * call, updates the current FileUsage object to reflect the known
 * initial state of the file.
 *
 * If do_read is false, it's assumed that the file was already opened
 * (or at least attempted to) for reading, and as such the initial
 * values are not changed; only the written property is updated.
 */
bool FileUsage::update_from_open_params(const std::string& filename, int flags, int err,
                                        bool do_read) {
  if (!do_read) {
    if (is_write(flags) && !err) {
      written_ = true;
    }
    return true;
  }

  if (!err) {
    if (is_write(flags)) {
      /* If successfully opened for writing:
       *
       *     trunc   creat   excl
       * A     +       -            => prev file must exist, contents don't matter
       * B     +       +       -    => prev file doesn't matter
       * C     +       +       +    => prev file mustn't exist
       * D     -       -            => prev file must exist, contents preserved and matter
       * E     -       +       -    => contents preserved (or new empty) and matter
       * F     -       +       +    => prev file mustn't exist
       */
      if ((flags & O_CREAT) && (flags & O_EXCL)) {
        /* C+F: If an exclusive new file was created, take a note that
         * the file didn't exist previously. */
        initial_state_ = NOTEXIST;
      } else if (flags & O_TRUNC) {
        if (!(flags & O_CREAT)) {
          /* A: What a nasty combo! We must take a note that the file
           * existed, but don't care about its previous contents (also
           * it's too late now to figure that out). */
          initial_state_ = EXIST;
        } else {
          /* B: The old contents could have been anything, we don't care
           * since we truncated. Keep initial_state_ = DONTCARE. */
        }
      } else {
        if (!(flags & O_CREAT)) {
          /* D: Another nasty combo. We can't distinguish a newly
           * created empty file from a previously empty one. If the file
           * is non-empty, we need to store its hash. */
          struct stat st;
          if (stat(filename.c_str(), &st) == -1) {
            unknown_err_ = errno;
            return false;
          }
          if (st.st_size == 0) {
            if (!initial_hash_.set_from_file(filename)) {
              unknown_err_ = errno;
              return false;
            }
            initial_state_ = EXIST_WITH_HASH;
          } else {
            initial_state_ = NOTEXIST_OR_EMPTY;
          }
        } else {
          /* E: Need to checksum the file. */
          if (!initial_hash_.set_from_file(filename)) {
            unknown_err_ = errno;
            return false;
          }
          initial_state_ = EXIST_WITH_HASH;
        }
      }
      written_ = true;
    } else {
      /* The file was successfully opened for reading only. */
      if (!initial_hash_.set_from_file(filename)) {
        unknown_err_ = errno;
        return false;
      }
      initial_state_ = EXIST_WITH_HASH;
    }
  } else /* if (err) */ {
    /* The attempt to open failed. */
    if (is_write(flags)) {
      /* Opening for writing failed. Could be a permission problem or so.
       * What to do? Probably nothing. */
      // FIXME...
    } else {
      /* Opening for reading failed. */
      if (err == ENOENT) {
        initial_state_ = NOTEXIST;
      } else {
        /* We don't support other errors such as permission denied. */
        unknown_err_ = err;
        return false;
      }
    }
  }
  return true;
}

}  // namespace firebuild
