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
#include "firebuild/utils.h"

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
      assert(0);
      return false;
    }
  }

  if (!S_ISREG(st->st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
    return false;
  }

  if (fb_copy_file_range(fd_src, NULL, fd_dst, NULL, st->st_size, 0) == st->st_size) {
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

/* /x/xx/<ascii key> */
static size_t kBlobCachePathLength = 1 + 1 + 1 + 2 + 1 + Hash::kAsciiLength;

/*
 * Constructs the filename where the cached file is to be stored, or
 * read from. Optionally creates the necessary subdirectories within the
 * cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key", and
 * create_dirs=true, it creates the directories "base/k" and "base/k/ke"
 * and returns "base/k/ke/key".
 */
static void construct_cached_file_name(const std::string &base, const Hash &key,
                                       bool create_dirs, char* path) {
  char ascii[Hash::kAsciiLength + 1];
  key.to_ascii(ascii);
  char *end = path;
  memcpy(end, base.c_str(), base.length());
  end += base.length();
  *end++ = '/'; *end++ = ascii[0];
  if (create_dirs) {
    *end = '\0';
    mkdir(path, 0700);
  }
  *end++ = '/'; *end++ = ascii[0]; *end++ = ascii[1];
  if (create_dirs) {
    *end = '\0';
    mkdir(path, 0700);
  }
  *end++ = '/';
  memcpy(end, ascii, sizeof(ascii));
}

/**
 * Store the given regular file in the blob cache, with its hash as the key.
 * Uses advanced technologies, such as copy on write, if available.
 *
 * If fd >= 0 then that is used as the data source, the path is only used for debugging.
 * If st != NULL then it contains the file's stat info.
 *
 * @param path The file to place in the cache
 * @param fd_src Optionally the opened file descriptor to copy
 * @param stat_ptr Optionally the file's parameters already stat()'ed
 * @param key_out Optionally store the key (hash) here
 * @return Whether succeeded
 */
bool BlobCache::store_file(const FileName *path,
                           int fd_src,
                           struct stat64 *stat_ptr,
                           Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, fd_src=%d", D(path), fd_src);

  FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: storing blob " + d(path));

  bool close_fd_src = false;
  if (fd_src == -1) {
    fd_src = open(path->c_str(), O_RDONLY);
    if (fd_src == -1) {
      perror("Failed opening file to be stored in cache");
      assert(0);
      return false;
    }
    close_fd_src = true;
  }

  struct stat64 st_local, *st;
  st = stat_ptr ? stat_ptr : &st_local;
  if (!stat_ptr && (fd_src >= 0 ? fstat64(fd_src, st) : stat64(path->c_str(), st)) == -1) {
    perror("Failed fstat64()-ing file to be stored in cache");
    assert(0);
    if (close_fd_src) {
      close(fd_src);
    }
    return false;
  }

  /* Copy the file to a temporary one under the cache */
  char *tmpfile;
  if (asprintf(&tmpfile, "%s/new.XXXXXX", base_dir_.c_str()) < 0) {
    perror("asprintf");
    assert(0);
    return false;
  }
  int fd_dst = mkstemp(tmpfile);  /* opens with O_RDWR */
  if (fd_dst == -1) {
    perror("Failed mkstemp() during storing file");
    assert(0);
    if (close_fd_src) {
      close(fd_src);
    }
    free(tmpfile);
    return false;
  }

  if (!copy_file(fd_src, fd_dst, st)) {
    FB_DEBUG(FB_DEBUG_CACHING, "failed to copy file");
    if (close_fd_src) {
      close(fd_src);
    }
    close(fd_dst);
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  if (close_fd_src) {
    close(fd_src);
  }

  /* Copying complete. Compute checksum on the copy, to prevent cache
   * corruption if someone is modifying the original file. */
  Hash key;
  /* Note that st belongs to fd_src, but we use it for fd_dst because the file type (regular file)
   * and the size are the same, and the rest are irrelevant. */
  if (!key.set_from_fd(fd_dst, st, NULL)) {
    FB_DEBUG(FB_DEBUG_CACHING, "failed to compute hash");
    close(fd_dst);
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  close(fd_dst);

  char* path_dst = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, true, path_dst);
  if (rename(tmpfile, path_dst) == -1) {
    perror("Failed renaming file while storing it");
    assert(0);
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  free(tmpfile);

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  => " + d(key));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place meta info in the cache, for easier debugging. */
    std::string path_debug = std::string(path_dst) + "_debug.txt";
    std::string txt(pretty_timestamp() + "  Copied from " + d(path) + "\n");
    int debugfd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    if (write(debugfd, txt.c_str(), txt.size()) < 0) {
      perror("BlobCache::store_file");
      assert(0);
    }
    close(debugfd);
  }

  if (key_out != NULL) {
    *key_out = key;
  }
  return true;
}

/**
 * Store the given regular file in the blob cache, with its hash as the key.
 *
 * The file is moved from its previous location. It is assumed that
 * no one modifies it during checksum computation, that is, the
 * intercepted processes have no direct access to it.
 *
 * The file handle is closed.
 *
 * The hash_cache is not queried or updated.
 *
 * This API is designed for PipeRecorder in order to place the recorded data in the cache.
 *
 * @param path The file to move to the cache
 * @param fd A fd referring to this file
 * @param len The file's length
 * @param key_out Optionally store the key (hash) here
 * @return Whether succeeded
 */
bool BlobCache::move_store_file(const std::string &path,
                                int fd,
                                size_t len,
                                Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, fd=%d, len=%ld", D(path), fd, len);

  FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: storing blob by moving " + path);

  Hash key;
  /* In order to save an fstat64() call in set_from_fd(), create a "fake" stat result here.
   * We know that it's a regular file, we know its size, and the rest are irrelevant. */
  struct stat64 st;
  st.st_mode = S_IFREG;
  st.st_size = len;
  if (!key.set_from_fd(fd, &st, NULL)) {
    FB_DEBUG(FB_DEBUG_CACHING, "failed to compute hash");
    close(fd);
    unlink(path.c_str());
    return false;
  }
  close(fd);

  char* path_dst = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, true, path_dst);
  if (rename(path.c_str(), path_dst) == -1) {
    perror("Failed renaming file to cache");
    assert(0);
    unlink(path.c_str());
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  => " + key.to_ascii());
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place meta info in the cache, for easier debugging. */
    std::string path_debug = std::string(path_dst) + "_debug.txt";
    std::string txt(pretty_timestamp() + "  Moved from " + path + "\n");
    int debugfd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    if (write(debugfd, txt.c_str(), txt.size()) < 0) {
      perror("BlobCache::move_store_file");
      assert(0);
      close(debugfd);
      return false;
    }
    close(debugfd);
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
  TRACK(FB_DEBUG_CACHING, "key=%s, path_dst=%s", D(key), D(path_dst));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: retrieving blob " + d(key) + " => " + d(path_dst));
  }

  char* path_src = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, false, path_src);

  int fd_src = open(path_src, O_RDONLY);
  if (fd_src == -1) {
    perror("Failed retrieving file from cache");
    assert(0);
    return false;
  }

  int fd_dst = open(path_dst->c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666);
  if (fd_dst == -1) {
    perror("Failed opening file to be recreated from cache");
    assert(0);
    close(fd_src);
    return false;
  }

  if (!copy_file(fd_src, fd_dst)) {
    FB_DEBUG(FB_DEBUG_CACHING, "Copying file from cache failed");
    assert(0);
    close(fd_src);
    close(fd_dst);
    unlink(path_dst->c_str());
    return false;
  }

  close(fd_src);
  close(fd_dst);
  return true;
}

/**
 * Get a read-only fd for a given entry in the cache.
 *
 * This is comfy when shortcutting a process and replaying what it wrote to a pipe.
 *
 * @param key The key (the file's hash)
 * @return A read-only fd, or -1
 */
int BlobCache::get_fd_for_file(const Hash &key) {
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: getting fd for blob " + key.to_ascii());
  }

  char* path_src = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, false, path_src);

  int fd = open(path_src, O_RDONLY);
  if (fd == -1) {
    perror("Failed open() in get_fd_for_file()");
    assert(0);
    return -1;
  }

  return fd;
}

}  /* namespace firebuild */
