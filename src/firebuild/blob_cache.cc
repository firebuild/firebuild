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

#include "firebuild/blob_cache.h"

#include <dirent.h>
#ifdef __APPLE__
#include <copyfile.h>
#endif
#include <fcntl.h>
#ifdef __linux__
#include <linux/fs.h>
#endif
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <tsl/hopscotch_set.h>

#include <vector>

#include "firebuild/ascii_hash.h"
#include "firebuild/execed_process_cacher.h"
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
#ifdef __APPLE__
  if (fcopyfile(fd_src, fd_dst, nullptr, COPYFILE_DATA) == 0) {
#else
    if (ioctl(fd_dst, FICLONE, fd_src) == 0) {
#endif
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

  off_t len = src_st->st_size >= src_skip_bytes ? src_st->st_size - src_skip_bytes : 0;
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

bool BlobCache::store_file(const FileName *path,
                           int max_writers,
                           int fd_src,
                           loff_t src_skip_bytes,
                           loff_t size,
                           Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, max_writers=%d, fd_src=%d, skip=%" PRIloff ", size=%" PRIloff,
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
  if (fb_renameat2(AT_FDCWD, tmpfile, AT_FDCWD, path_dst, RENAME_NOREPLACE) == -1) {
    if (errno == EEXIST) {
      FB_DEBUG(FB_DEBUG_CACHING, "blob is already stored");
      unlink(tmpfile);
    } else {
      fb_perror("Failed renaming file while storing it");
      assert(0);
      free(tmpfile);
      return false;
    }
  } else {
    execed_process_cacher->update_cached_bytes(dst_st.st_size);
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
    execed_process_cacher->update_cached_bytes(txt.size());
    close(debugfd);
  }

  if (key_out != NULL) {
    *key_out = key;
  }
  return true;
}

bool BlobCache::move_store_file(const std::string &path,
                                int fd,
                                loff_t size,
                                Hash *key_out) {
  TRACK(FB_DEBUG_CACHING, "path=%s, fd=%d, size=%" PRIloff, D(path), fd, size);

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
  if (fb_renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, path_dst, RENAME_NOREPLACE) == -1) {
    if (errno == EEXIST) {
      FB_DEBUG(FB_DEBUG_CACHING, "blob is already stored");
      unlink(path.c_str());
    } else {
      fb_perror("Failed renaming file to cache");
      assert(0);
      unlink(path.c_str());
      return false;
    }
  } else {
    execed_process_cacher->update_cached_bytes(size);
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "  => " + key.to_ascii());
  }

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Place meta info in the cache, for easier debugging. */
    std::string path_debug = std::string(path_dst) + kDebugPostfix;
    std::string txt(pretty_timestamp() + "  Moved from " + path + "\n");
    int debugfd = open(path_debug.c_str(), O_CREAT|O_WRONLY|O_APPEND, 0600);
    ssize_t written = write(debugfd, txt.c_str(), txt.size());
    if (written < 0) {
      fb_perror("BlobCache::move_store_file");
      assert(0);
      close(debugfd);
      return false;
    }
    execed_process_cacher->update_cached_bytes(written);
    close(debugfd);
  }

  if (key_out != NULL) {
    *key_out = key;
  }
  return true;
}

bool BlobCache::retrieve_file(int blob_fd,
                              const FileName *path_dst,
                              bool append) {
  TRACK(FB_DEBUG_CACHING, "blob_fd=%d, path_dst=%s, append=%s", blob_fd, D(path_dst), D(append));

  int flags = append ? O_WRONLY : (O_WRONLY|O_CREAT|O_TRUNC);
  int fd_dst = open(path_dst->c_str(), flags, 0666);
  if (fd_dst == -1) {
    if (append) {
      fb_perror("Failed opening file to be recreated from cache");
      assert(0);
    }
    return false;
  }

  if (!copy_file(blob_fd, 0, fd_dst, append)) {
    FB_DEBUG(FB_DEBUG_CACHING, "Copying file from cache failed");
    assert(0);
    close(blob_fd);
    close(fd_dst);
    if (!append) {
      unlink(path_dst->c_str());
    }
    return false;
  }

  close(fd_dst);
  return true;
}

int BlobCache::get_fd_for_file(const Hash &key) {
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    FB_DEBUG(FB_DEBUG_CACHING, "BlobCache: getting fd for blob " + key.to_ascii());
  }

  char* path_src = reinterpret_cast<char*>(alloca(base_dir_.length() + kBlobCachePathLength + 1));
  construct_cached_file_name(base_dir_, key, false, path_src);

  return open(path_src, O_RDONLY);
}

void BlobCache::delete_entries(const std::string& path,
                               const std::vector<std::string>& entries,
                               const std::string& debug_postfix,
                               off_t* debug_bytes) {
  struct stat st;
  for (const auto& entry : entries) {
    const std::string absolute_entry = path + "/" + entry;
    if (fstatat(AT_FDCWD, absolute_entry.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
      fb_perror(entry.c_str());
    } else {
      if (unlink(absolute_entry.c_str()) == 0) {
        execed_process_cacher->update_cached_bytes(-st.st_size);
      } else {
        fb_perror("unlink");
      }
    }
    if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
      const std::string absolute_debug_entry = absolute_entry + debug_postfix;
      if (fstatat(AT_FDCWD, absolute_debug_entry.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
        /* All debugging entries were kept in the previous round.
         * Delete the ones related to entries to be deleted. */
        if (unlink(absolute_debug_entry.c_str()) == 0) {
          execed_process_cacher->update_cached_bytes(-st.st_size);
          /* The size of this debug file has already been added to debug_bytes, reverse that. */
          *debug_bytes -= st.st_size;
        } else {
          fb_perror("unlink");
        }
      }
    }
  }
}

off_t BlobCache::gc_collect_total_blobs_size() {
  return recursive_total_file_size(base_dir_);
}

void BlobCache::gc_blob_cache_dir(const std::string& path,
                                  const tsl::hopscotch_set<AsciiHash>& referenced_blobs,
                                  off_t* cache_bytes, off_t* debug_bytes,
                                  off_t* unexpected_file_bytes) {
  DIR * dir = opendir(path.c_str());
  if (dir == NULL) {
    return;
  }

  /* Visit dirs recursively and check all the files. */
  struct dirent *dirent;
  std::vector<std::string> entries_to_delete;
  std::vector<std::string> subdirs_to_visit;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name = dirent->d_name;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }

    switch (fixed_dirent_type(dirent, dir, path)) {
      case DT_DIR: {
        subdirs_to_visit.push_back(name);
        break;
      }
      case DT_REG: {
        if (Hash::valid_ascii(name)) {
          if (referenced_blobs.find(AsciiHash(name)) == referenced_blobs.end()) {
            /* Not referenced, can be cleaned up*/
            entries_to_delete.push_back(name);
          } else {
            /* Good, keeping the referenced blob. */
            *cache_bytes += file_size(dir, name);
          }
          break;
        } else {
          /* Regular file, but not named as expected for a cache blob. */
          const char* debug_postfix = nullptr;
          if ((debug_postfix = strstr(name, kDebugPostfix))) {
            /* Files for debugging blobs.*/
            if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
              const size_t name_len = debug_postfix - name;
              assert_cmp(name_len, <, FB_PATH_BUFSIZE);
              char related_name[FB_PATH_BUFSIZE];
              memcpy(related_name, name, name_len);
              related_name[name_len] = '\0';
              struct stat st;
              if (fstatat(dirfd(dir), related_name, &st, 0) == 0) {
                /* Keeping debugging file that has related blob. If the object gets removed
                 * the debugging file will be removed with it, too. In that case debug_bytes
                 * needs to be adjusted again. */
                *debug_bytes += file_size(dir, name);
              } else {
                /* Removing old debugging file later to not break next readdir(). */
                entries_to_delete.push_back(name);
              }
            } else {
              /* Removing old debugging file later to not break next readdir(). */
              entries_to_delete.push_back(name);
            }
          } else {
            fb_error("Regular file among cache blobs has unexpected name, keeping it: " +
                     path + "/" + d(name));
            *unexpected_file_bytes += file_size(dir, name);
          }
        }
        break;
      }
      default:
        fb_error("File's type is unexpected, it is not a directory nor a regular file: " +
                 path + "/" + d(name));
    }
  }
  delete_entries(path, entries_to_delete, kDebugPostfix, debug_bytes);
  for (const auto& subdir : subdirs_to_visit) {
    gc_blob_cache_dir(path + "/" + subdir, referenced_blobs, cache_bytes, debug_bytes,
                      unexpected_file_bytes);
  }

  /* Remove empty directory. */
  rewinddir(dir);
  bool has_valid_entries = false;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name {dirent->d_name};
    /* skip "." and ".." */
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }
    has_valid_entries = true;
    break;
  }
  if (!has_valid_entries && path != base_dir_) {
    /* The directory is now empty. It can be removed. */
    rmdir(path.c_str());
  }
  closedir(dir);
}

void BlobCache::gc(const tsl::hopscotch_set<AsciiHash>& referenced_blobs, off_t* cache_bytes,
                   off_t* debug_bytes, off_t* unexpected_file_bytes) {
  gc_blob_cache_dir(base_dir_, referenced_blobs, cache_bytes, debug_bytes, unexpected_file_bytes);
}

}  /* namespace firebuild */
