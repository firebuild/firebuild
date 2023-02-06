/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
  if (!hash_cache->get_hash(filename_, max_writers_, &hash, &is_dir, &size)) {
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
  if (hash_cache->get_hash(filename_, max_writers_, &hash)) {
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
FileUsageUpdate FileUsageUpdate::get_from_open_params(
    const FileName *filename, int flags, mode_t mode_with_umask, int err, bool tmp_file) {
  TRACK(FB_DEBUG_PROC, "flags=%d, mode_with_umask=0%03o, err=%d, tmp_file=%s",
        flags, mode_with_umask, err, tmp_file ? "true" : "false");

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
         * previously, that the permissions will have to be set on it, and that its parent dir has
         * to exist. */
        update.set_initial_type(NOTEXIST);
        update.mode_changed_ = true;
        update.parent_type_ = ISDIR;
        update.tmp_file_ = tmp_file;
      } else if (flags & O_TRUNC) {
        assert(!tmp_file);
        if (!(flags & O_CREAT)) {
          /* A: What a nasty combo! We must take a note that the file existed, but don't care about
           * its previous contents (also it's too late now to figure that out). This implies that
           * the parent directory exists, no need to note that separately. */
          update.set_initial_type(ISREG);
        } else {
          /* B: The old contents could have been any regular file, or even no such file (but not
           * e.g. a directory). Also, the parent directory has to exist. */
          struct stat64 st;
          if (stat64(filename->c_str(), &st) < 0) {
            update.unknown_err_ = errno;
            return update;
          }
          if (st.st_size > 0) {
            /* We had O_TRUNC, so this is unexpected. */
            update.unknown_err_ = EEXIST;
            return update;
          }
          // FIXME handle if we see a directory. This cannot normally happen due to O_CREAT, but
          // can if the file has just been replaced by a directory.
          update.set_initial_type(NOTEXIST_OR_ISREG);
          if ((st.st_mode & 07777) != mode_with_umask) {
            /* A mode mismatch implies that the file necessarily existed before. See #861. */
            update.set_initial_type(ISREG);
          } else {
            /* The mode matches. The file may or may not have existed before, but in either case,
             * it'll have the given permissions now. Pretend that they were set explicitly. #861. */
            update.mode_changed_ = true;
          }
          update.parent_type_ = ISDIR;
        }
      } else {
        assert(!tmp_file);
        if (!(flags & O_CREAT)) {
          /* D: Contents unchanged. Need to checksum the file, we'll do that lazily. Implies that
           * the parent directory exists, no need to note that separately. */
          update.set_initial_type(ISREG);
          update.hash_computer_ = &FileUsageUpdate::hash_computer;
        } else {
          /* E: Another nasty combo. We can't distinguish a newly created empty file from a
           * previously empty one. If the file is non-empty, we need to store its hash. Also, the
           * parent directory has to exist. */
          struct stat64 st;
          if (stat64(filename->c_str(), &st) < 0) {
            update.unknown_err_ = errno;
            return update;
          }
          if (st.st_size > 0) {
            // FIXME handle if we see a directory. This cannot normally happen due to O_CREAT, but
            // can if the file has just been replaced by a directory.
            update.set_initial_type(ISREG);
            /* We got to know that this was a regular non-empty file. Delay hash computation until
             * necessary. */
            update.hash_computer_ = &FileUsageUpdate::hash_computer;
          } else {
            update.set_initial_type(NOTEXIST_OR_ISREG);
          }
          update.set_initial_size(st.st_size);
          if ((st.st_mode & 07777) != mode_with_umask) {
            /* A mode mismatch implies that the file necessarily existed before. See #861. */
            update.set_initial_type(ISREG);
          } else {
            /* The mode matches. The file may or may not have existed before, but in either case,
             * it'll have the given permissions now. Pretend that they were set explicitly. #861. */
            update.mode_changed_ = true;
          }
          update.parent_type_ = ISDIR;
        }
      }
      update.parent_type_ = ISDIR;
      update.written_ = true;
      update.max_writers_ = 1;
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
        if (!(flags & O_CREAT)) {
          /* If opening without O_CREAT failed then the file didn't exist. */
          update.set_initial_type(NOTEXIST);
        } else {
          /* When opening a file for writing the absence of the parent dir
           * results in NOTEXIST error. The grandparent dir could be missing as well,
           * but the missing parent dir would cause the same error thus it will not be a mistake
           * to shortcut the process if the parent dir is indeed missing. */
          update.parent_type_ = NOTEXIST;
        }
      } else if (err == EEXIST) {
        if (!tmp_file) {
          assert(flags & O_CREAT && flags & O_EXCL);
          update.set_initial_type(EXIST);
        } else {
          /* Could not create a unique temporary filename.  Now the contents of template are
           * undefined.*/
          update.set_initial_type(DONTKNOW);
          update.tmp_file_ = tmp_file;
          /* This error is actually known and handled, but it is safer to just prevent merging
           * this update by setting unknown_err_ because the path is undefined. */
          update.unknown_err_ = err;
        }
      } else if (err == ENOTDIR) {
        /* Occurs when opening the "foo/baz/bar" path when "foo/baz" is not a directory,
         * but for example a regular file. Or when "foo" is a regular file. We can't distinguish
         * between those cases, but if "/foo/baz" is a regular file we can safely shortcut the
         * process, because the process could not tell the difference either. */
        update.parent_type_ = ISREG;
      } else if (err == EINVAL) {
        update.set_initial_type(DONTKNOW);
        if (tmp_file) {
          /* Template was invalid, and is unmodified. We know nothing about that path. */
          update.set_initial_type(DONTKNOW);
          update.tmp_file_ = tmp_file;
        }
        /* This error is actually known and handled, but it is safer to just prevent merging
         * this update because the path is not used */
        update.unknown_err_ = err;
        return update;
      } else {
        /* We don't support other errors such as permission denied. */
        update.unknown_err_ = err;
        return update;
      }
    } else {
      assert(!tmp_file);
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
FileUsageUpdate FileUsageUpdate::get_from_mkdir_params(const FileName *filename, int err,
                                                       bool tmp_dir) {
  TRACK(FB_DEBUG_PROC, "err=%d", err);

  FileUsageUpdate update(filename);

  if (!err) {
    update.set_initial_type(NOTEXIST);
    update.parent_type_ = ISDIR;
    update.written_ = true;
    update.mode_changed_ = true;
    update.tmp_file_ = tmp_dir;
  } else {
    if (err == EEXIST) {
      /* The directory already exists. It may not be a directory, but in that case process inputs
       * will not match either. */
      update.set_initial_type(ISDIR);
    } else if (err == ENOENT) {
      /* A directory component in pathname does not exist or is a dangling symbolic link */
      // FIXME(rbalint) handle the dangling symlink case, too
      update.set_initial_type(NOTEXIST);
      update.parent_type_ = NOTEXIST;
    } else if (err == EINVAL) {
      update.set_initial_type(DONTKNOW);
      if (tmp_dir) {
        /* Template was invalid, and is unmodified. We know nothing about that path. */
        update.set_initial_type(DONTKNOW);
        update.tmp_file_ = tmp_dir;
        /* This error is actually known and handled, but it is safer to just prevent merging
         * this update by still setting unknown_err_ because the path is not used */
      }
      update.unknown_err_ = err;
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
                                                      off_t size, int err) {
  TRACK(FB_DEBUG_PROC, "mode=%d, size=%" PRIoff ", err=%d", mode, size, err);

  FileUsageUpdate update(filename);

  if (!err) {
    if (S_ISREG(mode)) {
      update.set_initial_type(ISREG);
      update.set_initial_mode_bits(mode, 07777 /* we know all the mode bits */);
      update.set_initial_size(size);
    } else if (S_ISDIR(mode)) {
      update.set_initial_type(ISDIR);
      update.set_initial_mode_bits(mode, 07777 /* we know all the mode bits */);
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

/**
 * Based on the parameters and return value of a rename() or similar call, returns a FileUsageUpdate
 * object that reflects how our usage of old file changed.
 */
FileUsageUpdate FileUsageUpdate::get_oldfile_usage_from_rename_params(const FileName *old_name,
                                                                      const FileName *new_name,
                                                                      int error) {
  TRACK(FB_DEBUG_PROC, "err=%d", error);
  /* Read the file's hash from the new location, but update generation from the old one's name
   * to keep the generation number increasing. Otherwise it would be reset to 1, which is valid
   * for the newly created file (if the file did not exist before). */
  // TODO(rbalint) Error handling is way more complicated for rename than for open, fix that here.
  FileUsageUpdate update = get_from_open_params(new_name, O_RDONLY, 0, error, false);
  update.written_ = true;
  update.mode_changed_ = true;
  update.generation_ = old_name->generation();

  return update;
}

/**
 * Based on the parameters and return value of a rename() or similar call, returns a FileUsageUpdate
 * object that reflects how our usage of new file changed.
 */
FileUsageUpdate FileUsageUpdate::get_newfile_usage_from_rename_params(const FileName *new_name,
                                                                      int error) {
  TRACK(FB_DEBUG_PROC, "err=%d", error);

  (void)error;  /* maybe unused */

  /* The file at the new name now necessarily exists. It may or may not empty, it doesn't matter. We
   * have to set mode_changed so that the mode will be restored when replaying from the cache.
   * This does not match any of the A..F cases of get_from_open_params(). */
  FileUsageUpdate update(new_name, DONTKNOW, true, true);

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
      ", mode_changed=" + d(mode_changed_) +
      ", generation=" + d(generation_) +
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
