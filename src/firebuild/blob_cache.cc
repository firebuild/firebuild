/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/blob_cache.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "firebuild/debug.h"
#include "firebuild/file_name.h"
#include "firebuild/hash.h"

namespace firebuild {

/* singleton */
BlobCache *blob_cache;

BlobCache::BlobCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}

/*
 * Copy the contents from an open file descriptor to another,
 * preferring advanced technologies like copy on write.
 */
static bool copy_file(int fd_src, int fd_dst, struct stat64 *st = NULL) {
  /* Try CoW first. */
  if (ioctl(fd_dst, FICLONE, fd_src) == 0) {
    /* CoW succeeded. Moo! */
    return true;
  }

  /* Try copy_file_range(). Gotta get the source file's size. */
  struct stat64 st_local;
  if (!st) {
    st = &st_local;
    if (fstat64(fd_src, st) == -1) {
      perror("fstat");
      return false;
    }
  }

  if (!S_ISREG(st->st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
    return false;
  }

  if (copy_file_range(fd_src, NULL, fd_dst, NULL, st->st_size, 0) == st->st_size) {
    /* copy_file_range() succeeded. */
    return true;
  }

  /* Try mmap() and write(). */
  void *p = NULL;
  if (st->st_size > 0) {
    /* Zero bytes can't be mmapped, we're fine with p == NULL then. */
    p = mmap(NULL, st->st_size, PROT_READ, MAP_SHARED, fd_src, 0);
  }
  if (p != MAP_FAILED) {
    // FIXME Do we need to handle short writes / EINTR?
    // FIXME Do we need to split large files into smaller writes?
    if (write(fd_dst, p, st->st_size) == st->st_size) {
      /* mmap() + write() succeeded. */
      munmap(p, st->st_size);
      return true;
    }
  }
  munmap(p, st->st_size);

  // FIXME Do we need to fallback to read() + write()? If so, may need to rewind fd_dst!!!

  return false;
}

/*
 * Constructs the filename where the cached file is to be stored, or
 * read from. Optionally creates the necessary subdirectories within the
 * cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key", and
 * create_dirs=true, it creates the directories "base/k" and "base/k/ke"
 * and returns "base/k/ke/key".
 */
static std::string construct_cached_file_name(const std::string &base,
                                              const Hash &key,
                                              bool create_dirs) {
  std::string key_str = key.to_ascii();
  std::string path = base + "/" + key_str.substr(0, 1);
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  path += "/" + key_str.substr(0, 2);
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  return path + "/" + key_str;
}

/**
 * Store the given regular file in the blob cache, with its hash as the key.
 * Uses advanced technologies, such as copy on write, if available.
 *
 * If fd >= 0 then that is used as the data source, the path is only used for debugging.
 * If st != NULL then it contains the file's stat info.
 *
 * @param path The file to place in the cache
 * @param key_out Optionally store the key (hash) here
 * @param fd_src Optionally the opened file descriptor to copy
 * @param stat_ptr Optionally the file's parameters already stat()'ed
 * @return Whether succeeded
 */
bool BlobCache::store_file(const FileName *path,
                           Hash *key_out,
                           int fd_src,
                           struct stat64 *stat_ptr) {
  FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: storing blob " + path->to_string());

  bool close_fd_src = false;
  if (fd_src == -1) {
    fd_src = open(path->c_str(), O_RDONLY);
    if (fd_src == -1) {
      perror("open");
      return false;
    }
    close_fd_src = true;
  }

  struct stat64 st_local, *st;
  st = stat_ptr ? stat_ptr : &st_local;
  if (!stat_ptr && (fd_src >= 0 ? fstat64(fd_src, st) : stat64(path->c_str(), st)) == -1) {
    perror("fstat64");
    if (close_fd_src) {
      close(fd_src);
    }
    return false;
  }

  /* Copy the file to a temporary one under the cache */
  std::string tmpfile = base_dir_ + "/new.XXXXXX";
  char *tmpfile_c_writable = &*tmpfile.begin();  // hack against c_str()'s constness
  int fd_dst = mkstemp(tmpfile_c_writable);  // opens with O_RDWR
  if (fd_dst == -1) {
    perror("mkstemp");
    if (close_fd_src) {
      close(fd_src);
    }
    return false;
  }

  if (!copy_file(fd_src, fd_dst, st)) {
    FB_DEBUG(FB_DEBUG_CACHING, "failed to copy file");
    if (close_fd_src) {
      close(fd_src);
    }
    close(fd_dst);
    unlink(tmpfile.c_str());
    return false;
  }
  if (close_fd_src) {
    close(fd_src);
  }

  /* Copying complete. Compute checksum on the copy, to prevent cache
   * corruption if someone is modifying the original file. */
  Hash key;
  if (!key.set_from_fd(fd_dst, NULL)) {
    FB_DEBUG(FB_DEBUG_CACHING, "failed to compute hash");
    close(fd_dst);
    unlink(tmpfile.c_str());
    return false;
  }
  close(fd_dst);

  std::string path_dst = construct_cached_file_name(base_dir_, key, true);
  if (rename(tmpfile.c_str(), path_dst.c_str()) == -1) {
    perror("rename");
    unlink(tmpfile.c_str());
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  => " + key.to_ascii());
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place meta info in the cache, for easier debugging. */
    std::string path_debug = path_dst + "_debug.txt";
    std::string txt(pretty_timestamp() + "  Copied from " + path->to_string() + "\n");
    int fd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    if (write(fd, txt.c_str(), txt.size()) < 0) {
      perror("BlobCache::store_file");
    }
    close(fd);
  }

  if (key_out != NULL) {
    *key_out = key;
  }
  return true;
}

/**
 * Retrieve the given file from the blob cache.
 *
 * The file is created with the default permissions, according to the
 * current umask.
 *
 * Uses advanced technologies, such as copy on write, if available.
 *
 * @param key The key (the file's hash)
 * @param path_dst Where to place the file
 * @return Whether succeeded
 */
bool BlobCache::retrieve_file(const Hash &key,
                              const FileName *path_dst) {
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: retrieving blob " + key.to_ascii() + " => "
             + path_dst->to_string());
  }

  std::string path_src = construct_cached_file_name(base_dir_, key, false);

  int fd_src = open(path_src.c_str(), O_RDONLY);
  if (fd_src == -1) {
    perror("open");
    return false;
  }

  int fd_dst = open(path_dst->c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666);
  if (fd_dst == -1) {
    perror("open");
    close(fd_src);
    return false;
  }

  if (!copy_file(fd_src, fd_dst)) {
    FB_DEBUG(FB_DEBUG_CACHING, "copying failed");
    close(fd_src);
    close(fd_dst);
    unlink(path_dst->c_str());
    return false;
  }

  close(fd_src);
  close(fd_dst);
  return true;
}

}  // namespace firebuild
