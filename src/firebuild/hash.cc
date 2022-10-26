/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include "firebuild/hash.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xxhash.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "firebuild/base64.h"
#include "firebuild/debug.h"
#include "firebuild/file_name.h"
#include "firebuild/utils.h"

namespace firebuild  {

void Hash::set_from_data(const void *data, ssize_t size) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "");

  /* xxhash's doc says:
   * "Streaming functions [...] is slower than single-call functions, due to state management."
   * Let's take the faster path. */
  hash_ = XXH3_128bits(data, size);
}

bool Hash::set_from_fd(int fd, const struct stat64 *stat_ptr, bool *is_dir_out, ssize_t *size_out) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "fd=%d, stat=%s", fd, D(stat_ptr));

  struct stat64 st_local;
  if (!stat_ptr && fstat64(fd, &st_local) == -1) {
    fb_perror("fstat");
    return false;
  }
  const struct stat64 *st = stat_ptr ? stat_ptr : &st_local;

  if (S_ISREG(st->st_mode)) {
    /* Compute the hash of a regular file. */
    if (is_dir_out != NULL) {
      *is_dir_out = false;
    }
    if (size_out != NULL) {
      *size_out = st->st_size;
    }

    void *map_addr;
    if (st->st_size > 0) {
      map_addr = mmap(NULL, st->st_size, PROT_READ, MAP_SHARED, fd, 0);
      if (map_addr == MAP_FAILED) {
        FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of regular file: mmap failed");
        return false;
      }
    } else {
      /* Zero length files cannot be mmapped. */
      map_addr = NULL;
    }

    set_from_data(map_addr, st->st_size);

    if (st->st_size > 0) {
      munmap(map_addr, st->st_size);
    }
    return true;

  } else if (S_ISDIR(st->st_mode)) {
    /* Compute the hash of a directory. Its listing is sorted, and
     * concatenated using '\0' as a terminator after each entry. Then
     * this string is hashed. */
    // FIXME place d_type in the string, too?
    if (is_dir_out != NULL) {
      *is_dir_out = true;
    }

    /* Quoting fdopendir(3):
     *   "After a successful call to fdopendir(), fd is used internally by the
     *   implementation, and should not otherwise be used by the application."
     * and closedir(3):
     *   "A successful call to closedir() also closes the underlying file descriptor"
     *
     * It would be an unconventional and hard to use API for this method to close the passed fd.
     * Not calling closedir() on the other hand could leave garbage in the memory, and
     * the caller of this method directly calling close() would also go against the manpage.
     * If we call closedir() and the caller also calls a failing close() then it's prone to
     * raceable errors if one day we go multithreaded or so.
     *
     * So work on a duplicated fd and eventually close that, while keeping the original fd opened.
     */
    DIR *dir = fdopendir(dup(fd));
    if (dir == NULL) {
      FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of directory: fdopendir failed");
      return false;
    }

    std::vector<std::string> listing;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      listing.push_back(entry->d_name);
    }
    closedir(dir);

    std::sort(listing.begin(), listing.end());

    std::string concat;
    for (const auto& entry : listing) {
      concat += entry;
      concat += '\0';
    }
    set_from_data(concat.c_str(), concat.size());
    return true;

  } else {
    FB_DEBUG(FB_DEBUG_HASH, "Cannot compute hash of special file");
    return false;
  }
}

bool Hash::set_from_file(const FileName *filename, const struct stat64 *stat_ptr,
                         bool *is_dir_out, ssize_t *size_out) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "filename=%s", D(filename));

  int fd;

  fd = open(filename->c_str(), O_RDONLY);
  if (fd == -1) {
    if (FB_DEBUGGING(FB_DEBUG_HASH)) {
      FB_DEBUG(FB_DEBUG_HASH, "File " + d(filename));
      fb_perror("open");
    }
    return false;
  }

  if (!set_from_fd(fd, stat_ptr, is_dir_out, size_out)) {
    close(fd);
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_HASH)) {
    FB_DEBUG(FB_DEBUG_HASH, "xxh64sum: " + d(filename) + " => " + d(this));
  }

  close(fd);
  return true;
}

void Hash::set(XXH128_hash_t value) {
  TRACKX(FB_DEBUG_HASH, 0, 1, Hash, this, "");

  hash_ = value;
}

/**
 * Get the ASCII representation.
 *
 * See the class's documentation for the exact format.
 */
void Hash::to_ascii(char *out) const {
  XXH128_canonical_t canonical;
  XXH128_canonicalFromHash(&canonical, hash_);

  Base64::encode(canonical.digest, out, sizeof(canonical.digest));
  out[kAsciiLength] = '\0';
}


/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Hash& hash, const int level) {
  (void)level;  /* unused */
  return hash.to_ascii();
}
std::string d(const Hash *hash, const int level) {
  if (hash) {
    return d(*hash, level);
  } else {
    return "{Hash NULL}";
  }
}

}  /* namespace firebuild */
