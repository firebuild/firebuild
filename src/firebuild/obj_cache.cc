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
#include <flatbuffers/minireflect.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "firebuild/cache_object_format_generated.h"
#pragma GCC diagnostic pop
#include "firebuild/debug.h"
#include "firebuild/hash.h"

namespace firebuild {

/* singleton */
ObjCache *obj_cache;

ObjCache::ObjCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}

/*
 * Constructs the directory name where the cached files are to be
 * stored, or read from. Optionally creates the necessary subdirectories
 * within the cache's base directory.
 *
 * Example: with base="base", key's ASCII representation being "key", and
 * create_dirs=true, it creates the directories "base/k", "base/k/ke"
 * and "base/k/ke/key" and returns the latter.
 */
static std::string construct_cached_dir_name(const std::string &base,
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
  path += "/" + key_str;
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  return path;
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
static std::string construct_cached_file_name(const std::string &base,
                                              const Hash &key,
                                              const Hash &subkey,
                                              bool create_dirs) {
  std::string path = construct_cached_dir_name(base, key, create_dirs);
  return path + "/" + subkey.to_ascii();
}

/* Replacement for flatbuffers::FlatBufferToString(), but with quoted keys. */
static std::string FlatBufferToStringQuoted(const uint8_t *buffer,
                                            const flatbuffers::TypeTable *type_table,
                                            bool multi_line = false,
                                            bool vector_delimited = true) {
  flatbuffers::ToStringVisitor tostring_visitor(multi_line ? "\n" : " ", true, "    ",
                                                vector_delimited);
  IterateFlatBuffer(buffer, type_table, &tostring_visitor);
  return tostring_visitor.s;
}


/**
 * Store a serialized entry in obj-cache.
 *
 * @param key The key
 * @param entry The entry to store
 * @param entry_len length of the entry to store
 * @param debug_key Optionally the key as pb for debugging purposes
 * @param subkey_out Optionally store the subkey (hash of the entry) here
 * @return Whether succeeded
 */
bool ObjCache::store(const Hash &key,
                     const uint8_t * const entry,
                     const size_t entry_len,
                     const uint8_t * const debug_key,
                     Hash *subkey_out) {
  TRACK(FB_DEBUG_CACHING, "key=%s", D(key));

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "ObjCache: storing entry, key " + d(key));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place a human-readable version of the key in the cache, for easier debugging. */
    std::string path_debug =
        construct_cached_dir_name(base_dir_, key, true) + "/%_directory_debug.json";
    std::string debug_text =
        FlatBufferToStringQuoted(debug_key, msg::ProcessFingerprintTypeTable(), true);

    int fd = creat(path_debug.c_str(), 0600);
    if (write(fd, debug_text.c_str(), debug_text.size()) < 0) {
      perror("store");
    }
    close(fd);
  }

  const char* tmpfile_end = "/new.XXXXXX";
  char* tmpfile = static_cast<char*>(malloc(base_dir_.length() + strlen(tmpfile_end) + 1));
  memcpy(tmpfile, base_dir_.c_str(), base_dir_.length());
  memcpy(tmpfile + base_dir_.length(), tmpfile_end, strlen(tmpfile_end) + 1);

  int fd_dst = mkstemp(tmpfile);  /* opens with O_RDWR */
  if (fd_dst == -1) {
    perror("mkstemp");
    free(tmpfile);
    return false;
  }

  Hash subkey;
  subkey.set_from_data(entry, entry_len);

  // FIXME Do we need to handle short writes / EINTR?
  // FIXME Do we need to split large files into smaller writes?
  auto written = write(fd_dst, entry, entry_len);
  if (written < 0 || static_cast<size_t>(written) != entry_len) {
    if (written == -1) {
      perror("write");
    } else {
      FB_DEBUG(FB_DEBUG_CACHING, "short write");
    }
    close(fd_dst);
    free(tmpfile);
    return false;
  }
  close(fd_dst);

  std::string path_dst = construct_cached_file_name(base_dir_, key, subkey, true);
  if (rename(tmpfile, path_dst.c_str()) == -1) {
    perror("rename");
    unlink(tmpfile);
    free(tmpfile);
    return false;
  }
  free(tmpfile);

  if (subkey_out != NULL) {
    *subkey_out = subkey;
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  value hash " + d(subkey));
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place a human-readable version of the value in the cache, for easier debugging. */
    std::string path_debug = path_dst + "_debug.json";
    std::string entry_txt =
        FlatBufferToStringQuoted(entry, firebuild::msg::ProcessInputsOutputsTypeTable(), true);

    int fd = creat(path_debug.c_str(), 0600);
    if (write(fd, entry_txt.c_str(), entry_txt.size()) < 0) {
      perror("store");
    }
    close(fd);
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

  std::string path = construct_cached_file_name(base_dir_, key, subkey, false);

  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    perror("open");
    return false;
  }

  struct stat64 st;
  if (fstat64(fd, &st) == -1) {
    perror("fstat");
    close(fd);
    return false;
  } else if (!S_ISREG(st.st_mode)) {
    FB_DEBUG(FB_DEBUG_CACHING, "not a regular file");
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
      close(fd);
      return false;
    }
  } else {
    fb_error("0-sized cache entry: " + path);
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
  std::string path = construct_cached_dir_name(base_dir_, key, false);

  DIR *dir = opendir(path.c_str());
  if (dir == NULL) {
    return ret;
  }

  Hash subkey;
  struct dirent *dirent;
  while ((dirent = readdir(dir)) != NULL) {
    if (subkey.set_hash_from_ascii(dirent->d_name)) {
      ret.push_back(subkey);
    }
  }

  closedir(dir);
  return ret;
}

}  // namespace firebuild
