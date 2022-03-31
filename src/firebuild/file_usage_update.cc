/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * FileUsageUpdate describes, for one particular Process and one particular filename, some pieces of
 * information that we get to know right now.
 *
 * Such structures are not stored in our long-term memory, these are ephemeral objects describing a
 * change that we quickly register.
 *
 * The differences from FileUsage are:
 *
 * - A FileUsageUpdate object exists on its own, rather than in a pool of unique objects.
 *
 * - A FileUsageUpdate object can describe that some information (e.g. type or hash) matters to us,
 *   but we haven't queried or computed it yet. This allows for lazy on-demand computation, and
 *   therefore save precious CPU time if the information isn't needed.
 *
 * - A FileUsageUpdate knows which file it belongs to, so it can perform the on-demand work on its own.
 *
 * - A FileUsageUpdate carries information about what to do with its parent directory, e.g. whether it
 *   needs to be registered that it must or must not exist.
 */

#include "firebuild/file_usage_update.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/hash_cache.h"

namespace firebuild {

/**
 * If we saw a successful open(..., O_RDONLY) then this method initializes the file type (regular
 * vs. directory) and the hash lazily on demand.
 */
void FileUsageUpdate::type_computer_open_rdonly() const {
  Hash hash;
  bool is_dir;
  ssize_t size = -1;
  if (!hash_cache->get_hash(filename_, &hash, &is_dir, &size)) {
    unknown_err_ = errno;
    return;
  }
  if (is_dir) {
    initial_state_.set_type(ISDIR);
    initial_state_.set_hash(hash);
  } else {
    initial_state_.set_type(ISREG);
    initial_state_.set_size(size);
    initial_state_.set_hash(hash);
  }
  type_computer_ = nullptr;
  hash_computer_ = nullptr;
}

/**
 * If we saw a successful open(..., O_WRONLY|O_CREAT) (without O_TRUNC and O_EXCL; perhaps with
 * O_RDWR instead of O_RDONLY) then the following two cases can happen:
 * - Now the file is empty. We cannot tell if the file existed and was empty before, or did not exist.
 * - Now the file is non-empty. We know that the file existed before with the current contents.
 * This method performs the lazy on-demand check to see which of these two happened.
 */
void FileUsageUpdate::type_computer_open_wronly_creat_notrunc_noexcl() const {
  struct stat st;
  if (stat(filename_->c_str(), &st) == -1) {
    unknown_err_ = errno;
    return;
  }
  if (st.st_size > 0) {
    // FIXME handle if we see a directory. This cannot normally happen due to O_CREAT, but can if
    // the file has just been replaced by a directory.
    initial_state_.set_type(ISREG);
    initial_state_.set_size(st.st_size);
    /* We got to know that this was a regular non-empty file. Delay hash computation until
     * necessary. */
    hash_computer_ = &FileUsageUpdate::hash_computer;
  } else {
    initial_state_.set_type(NOTEXIST_OR_ISREG_EMPTY);
  }
  type_computer_ = nullptr;
}

/**
 * Get the file type, looking it up on demand if necessary.
 *
 * Due to the nature of lazy lookup, an unexpected error can occur, in which case false is returned.
 */
bool FileUsageUpdate::get_initial_type(FileType *type_ptr) const {
  TRACKX(FB_DEBUG_PROC, 1, 1, FileUsageUpdate, this, "");

  if (type_computer_) {
    (this->*type_computer_)();
  }
  if (unknown_err_) {
    return false;
  } else {
    *type_ptr = initial_state_.type();
    return true;
  }
}

/**
 * This method executes the lazy on-demand retrieval or computation of the hash.
 */
void FileUsageUpdate::hash_computer() const {
  Hash hash;
  if (hash_cache->get_hash(filename_, &hash)) {
    initial_state_.set_hash(hash);
  } else {
    unknown_err_ = errno;
  }
  hash_computer_ = nullptr;
}

/**
 * Get the file hash, figuring it out on demand if necessary.
 *
 * Due to the nature of lazy lookup, an unexpected error can occur, in which case false is returned.
 */
bool FileUsageUpdate::get_initial_hash(Hash *hash_ptr) const {
  TRACKX(FB_DEBUG_PROC, 1, 1, FileUsageUpdate, this, "");

  assert(type_computer_ == nullptr &&
         (initial_state_.type() == ISREG || initial_state_.type() == ISDIR));

  if (hash_computer_) {
    (this->*hash_computer_)();
  }
  if (unknown_err_) {
    return false;
  } else {
    *hash_ptr = initial_state_.hash();
    return true;
  }
}

/**
 * Based on the parameters and return value of an open() or similar call, returns a FileUsageUpdate
 * object that reflects how our usage of this file changed.
 *
 * If the file's hash is important then we don't compute it yet but set hash_computer_ so that we
 * can compute it on demand.
 */
FileUsageUpdate FileUsageUpdate::get_from_open_params(const FileName *filename, int flags,
                                                      int err) {
  TRACK(FB_DEBUG_PROC, "flags=%d, err=%d", flags, err);

  FileUsageUpdate update(filename);

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
        /* C+F: If an exclusive new file was created, take a note that the file didn't exist
         * previously, but its parent dir has to exist. */
        update.set_initial_type(NOTEXIST);
        update.parent_type_ = ISDIR;
      } else if (flags & O_TRUNC) {
        if (!(flags & O_CREAT)) {
          /* A: What a nasty combo! We must take a note that the file existed, but don't care about
           * its previous contents (also it's too late now to figure that out). This implies that
           * the parent directory exists, no need to note that separately. */
          update.set_initial_type(ISREG);
        } else {
          /* B: The old contents could have been any regular file, or even no such file (but not
           * e.g. a directory). Also, the parent directory has to exist. */
          update.set_initial_type(NOTEXIST_OR_ISREG);
          update.parent_type_ = ISDIR;
        }
      } else {
        if (!(flags & O_CREAT)) {
          /* D: Contents unchanged. Need to checksum the file, we'll do that lazily. Implies that
           * the parent directory exists, no need to note that separately. */
          update.set_initial_type(ISREG);
          update.hash_computer_ = &FileUsageUpdate::hash_computer;
        } else {
          /* E: Another nasty combo. We can't distinguish a newly created empty file from a
           * previously empty one. If the file is non-empty, we need to store its hash. Also, the
           * parent directory has to exist. */
          update.type_computer_ = &FileUsageUpdate::type_computer_open_wronly_creat_notrunc_noexcl;
          update.parent_type_ = ISDIR;
        }
      }
      update.parent_type_ = ISDIR;
      update.written_ = true;
    } else {
      /* The file or directory was successfully opened for reading only.
       * Note that a plain open() can open a directory for reading, even without O_DIRECTORY. */
      update.type_computer_ = &FileUsageUpdate::type_computer_open_rdonly;
      update.hash_computer_ = &FileUsageUpdate::hash_computer;
    }
  } else /* if (err) */ {
    /* The attempt to open failed. */
    if (is_write(flags)) {
      if (err == ENOENT) {
        /* When opening a file for writing the absence of the parent dir
         * results NOTEXIST error. The grandparent dir could be missing as well,
         * but the missing parent dir would cause the same error thus it will not be a mistake
         * to shortcut the process if the parent dir is indeed missing. */
        update.parent_type_ = NOTEXIST;
      } else if (err == ENOTDIR) {
        /* Occurs when opening the "foo/baz/bar" path when "foo/baz" is not a directory,
         * but for example a regular file. Or when "foo" is a regular file. We can't distinguish
         * between those cases, but if "/foo/baz" is a regular file we can safely shortcut the
         * process, because the process could not tell the difference either. */
        update.parent_type_ = ISREG;
      } else {
        /* We don't support other errors such as permission denied. */
        update.unknown_err_ = err;
        return update;
      }
    } else {
      /* Opening for reading failed. */
      if (err == ENOENT) {
        update.set_initial_type(NOTEXIST);
      } else if (err == ENOTDIR) {
        /* See the comment in the is_write() branch. */
        update.parent_type_ = ISREG;
      } else {
        /* We don't support other errors such as permission denied. */
        update.unknown_err_ = err;
        return update;
      }
    }
  }

  return update;
}

/**
 * Based on the parameters and return value of an mkdir() call, returns a FileUsageUpdate object that
 * reflects how our usage of this file changed.
 */
FileUsageUpdate FileUsageUpdate::get_from_mkdir_params(const FileName *filename, int err) {
  TRACK(FB_DEBUG_PROC, "err=%d", err);

  FileUsageUpdate update(filename);

  if (!err) {
    update.set_initial_type(NOTEXIST);
    update.parent_type_ = ISDIR;
    update.written_ = true;
  } else {
    if (err == EEXIST) {
      /* The directory already exists. It may not be a directory, but in that case process inputs
       * will not match either. */
      update.set_initial_type(ISDIR);
    } else {
      /* We don't support other errors such as permission denied. */
      update.unknown_err_ = err;
    }
  }

  return update;
}

/**
 * Based on the parameters and return value of a stat() or similar call, returns a FileUsageUpdate
 * object that reflects how our usage of this file changed.
 */
FileUsageUpdate FileUsageUpdate::get_from_stat_params(const FileName *filename, mode_t mode,
                                                      int err) {
  TRACK(FB_DEBUG_PROC, "mode=%d, err=%d", mode, err);

  FileUsageUpdate update(filename);

  if (!err) {
    if (S_ISREG(mode)) {
      update.set_initial_type(ISREG);
    } else if (S_ISDIR(mode)) {
      update.set_initial_type(ISDIR);
    } else if (S_ISLNK(mode)) {
      /* It's a symlink. We got to know absolutely nothing about the underlying file, directory, or
       * lack thereof. FIXME: Refine this logic as per #784. */
      update.set_initial_type(DONTKNOW);
    } else {
      /* Neither regular file nor directory. Pretend for now that there's nothing there. */
      update.set_initial_type(NOTEXIST);
    }
  } else {
    update.set_initial_type(NOTEXIST);
  }

  return update;
}

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string FileUsageUpdate::d_internal(const int level) const {
  (void)level;  /* unused */
  return std::string("{FileUsageUpdate initial_state=") + d(initial_state_, level) +
      (type_computer_ ? ", type_computer=<func>" : "") +
      (hash_computer_ ? ", hash_computer=<func>" : "") +
      ", written=" + d(written_) +
      ", unknown_err=" + d(unknown_err_) + "}";
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileUsageUpdate& fuu, const int level) {
  return fuu.d_internal(level);
}
std::string d(const FileUsageUpdate *fuu, const int level) {
  if (fuu) {
    return d(*fuu, level);
  } else {
    return "{FileUsageUpdate NULL}";
  }
}

}  /* namespace firebuild */
