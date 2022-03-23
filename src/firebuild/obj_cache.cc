/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/*
 * obj-cache is a weird caching structure where a key can contain
 * multiple values. More precisely, a key contains a list of subkeys,
 * and a (key, subkey) pair points to a value.
 *
 * In practice, one ProcessFingerprint can have multiple
 * ProcessInputsOutputs associated with it. The key is the hash of
 * ProcessFingerprint's serialization. The subkey happens to be the hash
 * of ProcessInputsOutputs's serialization, although it could easily be
 * anything else.
 *
 * Currently the backend is the filesystem. The multiple values are
 * stored as separate file of a given directory. The list of subkeys is
 * retrieved by listing the directory.
 *
 * E.g. ProcessFingerprint1's hash in ASCII is "fingerprint1". Underneath
 * it there are two values: ProcessInputsOutputs1's hash in ASCII is
 * "inputsoutputs1",ProcessInputsOutputs2's hash in ASCII is
 * "inputsoutputs2". The directory structure is:
 * - f/fi/fingerprint1/inputsoutputs1
 * - f/fi/fingerprint1/inputsoutputs2
 */

#include "firebuild/obj_cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "firebuild/debug.h"
#include "firebuild/hash.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"

namespace firebuild {

/* singleton */
ObjCache *obj_cache;

ObjCache::ObjCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}


/* /x/xx/<ascii key>/<ascii subkey> */
static size_t kObjCachePathLength = 1 + 1 + 1 + 2 + 1 + 2 * Hash::kAsciiLength + 1;

/*
 * Constructs the directory name where the cached files are to be
 * stored, or read from. Optionally creates the necessary subdirectories
 * within the cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key", and
 * create_dirs=true, it creates the directories "base/k", "base/k/ke"
 * and "base/k/ke/key" and returns the latter.
 */
static void construct_cached_dir_name(const std::string &base, const Hash &key,
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
  if (create_dirs) {
    mkdir(path, 0700);
  }
}

/*
 * Constructs the filename where the cached file is to be stored, or
 * read from. Optionally creates the necessary subdirectories within the
 * cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key",
 * subkey's ASCII representation being "subkey", and create_dirs=true, it
 * creates the directories "base/k", "base/k/ke" and "base/k/ke/key" and
 * returns "base/k/ke/key/subkey".
 */
static void construct_cached_file_name(const std::string &base,
                                       const Hash &key,
                                       const Hash &subkey,
                                       bool create_dirs,
                                       char* path) {
  construct_cached_dir_name(base, key, create_dirs, path);
  path[base.length() + kObjCachePathLength - Hash::kAsciiLength - 1] = '/';
  subkey.to_ascii(&path[base.length() + kObjCachePathLength - Hash::kAsciiLength]);
}


/**
 * Store a serialized entry in obj-cache.
 *
 * @param key The key
 * @param entry The entry to serialize and store
 * @param debug_key Optionally the key as pb for debugging purposes
 * @return Whether succeeded
 */
bool ObjCache::store(const Hash &key,
                     const FBBSTORE_Builder * const entry,
                     const FBBFP_Serialized * const debug_key) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: storing entry, key " + d(key));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place a human-readable version of the key in the cache, for easier debugging. */
    const char* debug_postfix = "/%_directory_debug.json";
    char* path_debug =
        reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength
                                       - Hash::kAsciiLength + strlen(debug_postfix) + 1));
    construct_cached_dir_name(base_dir_, key, true, path_debug);
    memcpy(&path_debug[base_dir_.length() + kObjCachePathLength - Hash::kAsciiLength - 1],
           debug_postfix, strlen(debug_postfix) + 1);

    FILE *f = fopen(path_debug, "w");
    fbbfp_serialized_debug(f, debug_key);
    fclose(f);
  }

  const char* tmpfile_end = "/new.XXXXXX";
  char* tmpfile = static_cast<char*>(malloc(base_dir_.length() + strlen(tmpfile_end) + 1));
  memcpy(tmpfile, base_dir_.c_str(), base_dir_.length());
  memcpy(tmpfile + base_dir_.length(), tmpfile_end, strlen(tmpfile_end) + 1);

  int fd_dst = mkstemp(tmpfile);  /* opens with O_RDWR */
  if (fd_dst == -1) {
    perror("Failed mkstemp() for storing cache object");
    assert(0);
    free(tmpfile);
    return false;
  }

  // FIXME Do we need to handle short writes / EINTR?
  // FIXME Do we need to split large files into smaller writes?
  // FIXME add basic error handling
  // FIXME Is it faster if we alloca() for small sizes instead of malloc()?
  // FIXME Is it faster to ftruncate() the file to the desired size, then mmap,
  // then serialize to the mapped memory, then ftruncate() again to the actual size?
  size_t len = fbbstore_builder_measure(entry);
  char *entry_serial = reinterpret_cast<char *>(malloc(len));
  fbbstore_builder_serialize(entry, entry_serial);
  fb_write(fd_dst, entry_serial, len);
  close(fd_dst);

  /* Create randomized object file */
  char* path_dst = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  // TODO(rbalint) user shorter subkey and use different alphabet to
  // make it more apparent that the subkey is not a hash of anything
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  Hash subkey({static_cast<uint64_t>(time.tv_sec), static_cast<uint64_t>(time.tv_nsec)});
  if (FB_DEBUGGING(FB_DEBUG_CACHESORT)) {
    /* Debugging: Instead of a randomized filename (which is fast to generate) use the content's
     * hash for a deterministic filename. */
    subkey.set_from_data(entry_serial, len);
  }
  construct_cached_file_name(base_dir_, key, subkey, true, path_dst);
  free(entry_serial);

  if (rename(tmpfile, path_dst) == -1) {
    perror("Failed rename() while storing cache object");
    assert(0);
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  subkey " + d(subkey));
  }
  free(tmpfile);

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place a human-readable version of the value in the cache, for easier debugging. */
    const char* debug_postfix = "_debug.json";
    char* path_debug = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength
                                                      + strlen(debug_postfix) + 1));
    memcpy(path_debug, path_dst, base_dir_.length() + kObjCachePathLength);
    memcpy(&path_debug[base_dir_.length() + kObjCachePathLength], debug_postfix,
           strlen(debug_postfix) + 1);

    FILE *f = fopen(path_debug, "w");
    fbbstore_builder_debug(f, entry);
    fclose(f);
  }
  return true;
}

/**
 * Retrieve an entry from the obj-cache.
 *
 * @param key The key
 * @param subkey The subkey
 * @param[out] entry mmap()-ed cache entry. It is the caller's responsibility to munmap() it later.
 * @param[out] entry_len entry's length in bytes
 * @return Whether succeeded
 */
bool ObjCache::retrieve(const Hash &key,
                        const Hash &subkey,
                        uint8_t ** entry,
                        size_t * entry_len) {
  TRACK(FB_DEBUG_CACHING, "key=%s, subkey=%s", D(key), D(subkey));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: retrieving entry, key "
             + d(key) + " subkey " + d(subkey));
  }

  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, subkey, false, path);

  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    perror("open");
    assert(0);
    return false;
  }

  struct stat64 st;
  if (fstat64(fd, &st) == -1) {
    perror("Failed fstat retrieving cache object");
    assert(0);
    close(fd);
    return false;
  } else if (!S_ISREG(st.st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
    assert(0);
    close(fd);
    return false;
  }

  uint8_t *p = NULL;
  if (st.st_size > 0) {
    /* Zero bytes can't be mmapped, we're fine with p == NULL then.
     * Although a serialized entry probably can't be 0 bytes long. */
    p = reinterpret_cast<uint8_t*>(mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
      perror("mmap");
      assert(0);
      close(fd);
      return false;
    }
  } else {
    fb_error("0-sized cache entry: " + std::string(path));
    assert(st.st_size <= 0);
    close(fd);
    return false;
  }
  close(fd);

  *entry_len = st.st_size;
  *entry = p;
  return true;
}

/**
 * Return the list of subkeys for the given key.
 *
 * // FIXME return them in some particular order??
 *
 * // FIXME replace with some iterator-like approach?
 */
std::vector<Hash> ObjCache::list_subkeys(const Hash &key) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  std::vector<Hash> ret;
  char* path = reinterpret_cast<char*>(alloca(base_dir_.length() + kObjCachePathLength + 1));
  construct_cached_dir_name(base_dir_, key, false, path);

  DIR *dir = opendir(path);
  if (dir == NULL) {
    return ret;
  }

  Hash subkey;
  struct dirent *dirent;
  while ((dirent = readdir(dir)) != NULL) {
    if (subkey.set_from_ascii(dirent->d_name)) {
      ret.push_back(subkey);
    }
  }

  closedir(dir);
  return ret;
}

}  /* namespace firebuild */
