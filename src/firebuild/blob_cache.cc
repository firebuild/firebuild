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
 * Might skip the beginning of the input file.
 * Might append to the target file instead of replacing its contents. O_APPEND should _not_ be set
 * on fd_dst because then the fast copy_file_range() method doesn't work.
 */
static bool copy_file(int fd_src, loff_t src_skip_bytes, int fd_dst, bool append,
                      const struct stat64 *src_stat_ptr = NULL) {
  /* Try CoW first. */
  if (src_skip_bytes == 0 && !append) {
    if (ioctl(fd_dst, FICLONE, fd_src) == 0) {
      /* CoW succeeded. Moo! */
      return true;
    }
  } else {
    // FIXME try FICLONERANGE
  }

  /* Try copy_file_range(). Gotta get the source file's size, and in append mode also the
   * destination file's size */
  struct stat64 src_st_local;
  if (!src_stat_ptr && fstat64(fd_src, &src_st_local) == -1) {
    fb_perror("fstat");
    assert(0);
    return false;
  }
  const struct stat64 *src_st = src_stat_ptr ? src_stat_ptr : &src_st_local;
  if (!S_ISREG(src_st->st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
    return false;
  }

  struct stat64 dst_st_local;
  const struct stat64 *dst_st = nullptr;
  if (append) {
    if (fstat64(fd_dst, &dst_st_local) == -1) {
      fb_perror("fstat");
      assert(0);
      return false;
    }
    dst_st = &dst_st_local;
    if (!S_ISREG(dst_st->st_mode)) {
      FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
      return false;
    }
  }

  ssize_t len = src_st->st_size >= src_skip_bytes ? src_st->st_size - src_skip_bytes : 0;
  loff_t dst_skip_bytes = append ? dst_st->st_size : 0;
  return fb_copy_file_range(fd_src, &src_skip_bytes, fd_dst, &dst_skip_bytes, len, 0) == len;
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
 *
 * @param path The file to place in the cache
 * @param max_writers Maximum allowed number of writers to this file
 * @param fd_src Optionally the opened file descriptor to copy
 * @param src_skip_bytes Number of bytes to omit from the beginning of the input file
 * @param size The file's size (including the bytes to be skipped)
 * @param key_out Optionally store the key (hash) here
 * @return Whether succeeded
 */
bool BlobCache::store_file(const FileName *path,
                           int max_writers,
                           int fd_src,
                           loff_t src_skip_bytes,
                           size_t size,
                           Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, max_writers=%d, fd_src=%d, skip=%ld, size=%ld",
      D(path), max_writers, fd_src, src_skip_bytes, size);

  FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: storing blob " + d(path));

  if (path->writers_count() > max_writers) {
    /* The file could be written while saving the file, don't take that risk. */
    FB_DEBUG(FB_DEBUG_CACHING, "file is opened for writing by some other process");
    return false;
  }

  bool close_fd_src = false;
  if (fd_src == -1) {
    fd_src = open(path->c_str(), O_RDONLY);
    if (fd_src == -1) {
      fb_perror("Failed opening file to be stored in cache");
      assert(0);
      return false;
    }
    close_fd_src = true;
  }

  /* In order to save an fstat64() call in copy_file() and set_from_fd(), create a "fake" stat
   * result here. We know it's a regular file, we know its size, and the rest are irrelevant. */
  struct stat64 src_st;
  src_st.st_mode = S_IFREG;
  src_st.st_size = size;

  /* Copy the file to a temporary one under the cache */
  char *tmpfile;
  if (asprintf(&tmpfile, "%s/new.XXXXXX", base_dir_.c_str()) < 0) {
    fb_perror("asprintf");
    assert(0);
    return false;
  }
  int fd_dst = mkstemp(tmpfile);  /* opens with O_RDWR */
  if (fd_dst == -1) {
    fb_perror("Failed mkstemp() during storing file");
    assert(0);
    if (close_fd_src) {
      close(fd_src);
    }
    free(tmpfile);
    return false;
  }

  if (!copy_file(fd_src, src_skip_bytes, fd_dst, false, &src_st)) {
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
  /* In order to save an fstat64() call in set_from_fd(), create a "fake" stat result here. We
   * know that it's a regular file, we know its size, and the rest are irrelevant. */
  struct stat64 dst_st;
  dst_st.st_mode = S_IFREG;
  dst_st.st_size = src_st.st_size >= src_skip_bytes ? src_st.st_size - src_skip_bytes : 0;
  if (!key.set_from_fd(fd_dst, &dst_st, NULL)) {
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
    fb_perror("Failed renaming file while storing it");
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
    std::string path_debug = std::string(path_dst) + kDebugPostfix;
    std::string txt(pretty_timestamp() + "  Copied from " + d(path) + "\n");
    int debugfd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    if (write(debugfd, txt.c_str(), txt.size()) < 0) {
      fb_perror("BlobCache::store_file");
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
 * @param size The file's size
 * @param key_out Optionally store the key (hash) here
 * @return Whether succeeded
 */
bool BlobCache::move_store_file(const std::string &path,
                                int fd,
                                size_t size,
                                Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, fd=%d, size=%ld", D(path), fd, size);

  FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: storing blob by moving " + path);

  Hash key;
  /* In order to save an fstat64() call in set_from_fd(), create a "fake" stat result here.
   * We know that it's a regular file, we know its size, and the rest are irrelevant. */
  struct stat64 st;
  st.st_mode = S_IFREG;
  st.st_size = size;
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
    fb_perror("Failed renaming file to cache");
    assert(0);
    unlink(path.c_str());
    return false;
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  => " + key.to_ascii());
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place meta info in the cache, for easier debugging. */
    std::string path_debug = std::string(path_dst) + kDebugPostfix;
    std::string txt(pretty_timestamp() + "  Moved from " + path + "\n");
    int debugfd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    if (write(debugfd, txt.c_str(), txt.size()) < 0) {
      fb_perror("BlobCache::move_store_file");
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
 * In non-append mode the file doesn't have to exist. If it doesn't exist, it's created with the
 * default permissions, according to the current umask. If it already exists, its contents will be
 * replaced, the permissions will be left unchanged.
 *
 * In append mode the file must already exist, the cache entry will be appended to it.
 *
 * Uses advanced technologies, such as copy on write, if available.
 *
 * @param key The key (the file's hash)
 * @param path_dst Where to place the file
 * @param append Whether to use append mode
 * @return Whether succeeded
 */
bool BlobCache::retrieve_file(const Hash &key,
                              const FileName *path_dst,
                              bool append) {
  TRACK(FB_DEBUG_CACHING, "key=%s, path_dst=%s, append=%s", D(key), D(path_dst), D(append));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: retrieving blob " + d(key) + " => " + d(path_dst));
  }

  char* path_src = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, false, path_src);

  int fd_src = open(path_src, O_RDONLY);
  if (fd_src == -1) {
    fb_perror("Failed retrieving file from cache");
    assert(0);
    return false;
  }

  int flags = append ? O_WRONLY : (O_WRONLY|O_CREAT|O_TRUNC);
  int fd_dst = open(path_dst->c_str(), flags, 0666);
  if (fd_dst == -1) {
    if (append) {
      fb_perror("Failed opening file to be recreated from cache");
      assert(0);
    }
    return false;
  }

  if (!copy_file(fd_src, 0, fd_dst, append)) {
    FB_DEBUG(FB_DEBUG_CACHING, "Copying file from cache failed");
    assert(0);
    close(fd_src);
    close(fd_dst);
    if (!append) {
      unlink(path_dst->c_str());
    }
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
    fb_perror("Failed open() in get_fd_for_file()");
    assert(0);
    return -1;
  }

  return fd;
}

}  /* namespace firebuild */
