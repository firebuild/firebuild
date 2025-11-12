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

#include "firebuild/pipe_recorder.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cassert>

#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/config.h"
#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#include "firebuild/hash.h"
#include "firebuild/pipe.h"
#include "firebuild/utils.h"

namespace firebuild {

PipeRecorder::PipeRecorder(const ExecedProcess *for_proc)
    : for_proc_(for_proc), id_(id_counter_++) {
  TRACKX(FB_DEBUG_PIPE, 0, 1, PipeRecorder, this, "for_proc=%s", D(for_proc));
}

void PipeRecorder::open_backing_file() {
  TRACKX(FB_DEBUG_PIPE, 1, 0, PipeRecorder, this, "");

  if (asprintf(&filename_, "%s/pipe.XXXXXX", base_dir_) < 0) {
    fb_perror("asprintf");
    assert(0 && "asprintf");
  }
  fd_ = mkstemp(filename_);  /* opens with O_RDWR */
  if (fd_ < 0) {
    fb_perror("mkstemp");
    free(filename_);
    filename_ = NULL;
    assert(0 && "mkstemp");
  }
}

void PipeRecorder::add_data_from_buffer(const char *buf, ssize_t len) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "len=%" PRIssize, len);

  assert(!deactivated_);
  assert(!abandoned_);
  assert_cmp(len, >, 0);

  /* Check if we should use memory buffer for small data */
  if (use_memory_buffer_ && (offset_ + len) <= max_inline_blob_size) {
    /* Allocate or grow the memory buffer as needed */
    if (mem_buffer_capacity_ < static_cast<size_t>(offset_ + len)) {
      size_t new_capacity = offset_ + len;
      char *new_buffer = static_cast<char*>(realloc(mem_buffer_, new_capacity));
      if (!new_buffer) {
        fb_perror("realloc");
        /* Fall back to file-based storage */
        use_memory_buffer_ = false;
        if (mem_buffer_) {
          free(mem_buffer_);
          mem_buffer_ = NULL;
          mem_buffer_capacity_ = 0;
        }
      } else {
        mem_buffer_ = new_buffer;
        mem_buffer_capacity_ = new_capacity;
      }
    }

    if (use_memory_buffer_) {
      /* Copy data to memory buffer */
      memcpy(mem_buffer_ + offset_, buf, len);
      offset_ += len;
      assert_cmp(offset_, >, 0);
      return;
    }
    /* If realloc failed, fall through to file-based storage */
  } else if (use_memory_buffer_) {
    /* Data exceeds threshold, switch to file-based storage */
    use_memory_buffer_ = false;
  }

  /* File-based storage */
  if (fd_ < 0) {
    open_backing_file();
    /* If we have buffered data, write it to the file first */
    if (mem_buffer_ && offset_ > 0) {
      fb_write(fd_, mem_buffer_, offset_);
      free(mem_buffer_);
      mem_buffer_ = NULL;
      mem_buffer_capacity_ = 0;
    }
  }

#ifndef NDEBUG
  ssize_t saved =
#endif
      fb_write(fd_, buf, len);
  assert_cmp(saved, ==, len);

  offset_ += len;
  assert_cmp(offset_, >, 0);
}

void PipeRecorder::add_data_from_unix_pipe(int pipe_fd, ssize_t len) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "pipe_fd=%d, len=%" PRIssize, pipe_fd, len);

  assert(!deactivated_);
  assert(!abandoned_);
  assert_cmp(len, >, 0);

  /* For pipes, we can't easily buffer in memory, so switch to file-based storage */
  if (use_memory_buffer_ && (offset_ + len) > max_inline_blob_size) {
    use_memory_buffer_ = false;
  }

  if (use_memory_buffer_ && (offset_ + len) <= max_inline_blob_size) {
    /* Allocate or grow the memory buffer */
    if (mem_buffer_capacity_ < static_cast<size_t>(offset_ + len)) {
      size_t new_capacity = offset_ + len;
      char *new_buffer = static_cast<char*>(realloc(mem_buffer_, new_capacity));
      if (!new_buffer) {
        fb_perror("realloc");
        use_memory_buffer_ = false;
        if (mem_buffer_) {
          free(mem_buffer_);
          mem_buffer_ = NULL;
          mem_buffer_capacity_ = 0;
        }
      } else {
        mem_buffer_ = new_buffer;
        mem_buffer_capacity_ = new_capacity;
      }
    }

    if (use_memory_buffer_) {
      /* Read from pipe into memory buffer */
      ssize_t read_bytes = read(pipe_fd, mem_buffer_ + offset_, len);
      if (read_bytes != len) {
        fb_perror("read from pipe");
        assert(0);
      }
      offset_ += len;
      assert_cmp(offset_, >, 0);
      return;
    }
  }

  if (fd_ < 0) {
    open_backing_file();
    /* If we have buffered data, write it to the file first */
    if (mem_buffer_ && offset_ > 0) {
      fb_write(fd_, mem_buffer_, offset_);
      free(mem_buffer_);
      mem_buffer_ = NULL;
      mem_buffer_capacity_ = 0;
    }
  }

  /* Writing to a regular file. Also the caller must make sure by a preceding tee(2) call that
   * the given amount of data is readily available. So we're not expecting short writes. */
#ifndef NDEBUG
  ssize_t saved =
#endif
#ifdef __linux__
      splice(pipe_fd, NULL, fd_, NULL, len, 0);
#else
      fb_copy_file_range(pipe_fd, NULL, fd_, NULL, len, 0);
#endif
  assert_cmp(saved, ==, len);

  offset_ += len;
  assert_cmp(offset_, >, 0);
}

void PipeRecorder::add_data_from_regular_fd(int fd_in, loff_t off_in, ssize_t len) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "fd_in=%d, off_in=%" PRIloff ", len=%" PRIssize,
         fd_in, off_in, len);

  assert(fd_in >= 0);
  assert(!deactivated_);
  assert(!abandoned_);
  assert_cmp(len, >, 0);

  /* For regular files, switch to file-based storage if exceeds threshold */
  if (use_memory_buffer_ && (offset_ + len) > max_inline_blob_size) {
    use_memory_buffer_ = false;
  }

  if (use_memory_buffer_ && (offset_ + len) <= max_inline_blob_size) {
    /* Allocate or grow the memory buffer */
    if (mem_buffer_capacity_ < static_cast<size_t>(offset_ + len)) {
      size_t new_capacity = offset_ + len;
      char *new_buffer = static_cast<char*>(realloc(mem_buffer_, new_capacity));
      if (!new_buffer) {
        fb_perror("realloc");
        use_memory_buffer_ = false;
        if (mem_buffer_) {
          free(mem_buffer_);
          mem_buffer_ = NULL;
          mem_buffer_capacity_ = 0;
        }
      } else {
        mem_buffer_ = new_buffer;
        mem_buffer_capacity_ = new_capacity;
      }
    }

    if (use_memory_buffer_) {
      /* Read from file into memory buffer */
      ssize_t read_bytes = pread(fd_in, mem_buffer_ + offset_, len, off_in);
      if (read_bytes != len) {
        fb_perror("pread from file");
        assert(0);
      }
      offset_ += len;
      assert_cmp(offset_, >, 0);
      return;
    }
  }

  if (fd_ < 0) {
    open_backing_file();
    /* If we have buffered data, write it to the file first */
    if (mem_buffer_ && offset_ > 0) {
      fb_write(fd_, mem_buffer_, offset_);
      free(mem_buffer_);
      mem_buffer_ = NULL;
      mem_buffer_capacity_ = 0;
    }
  }

  ssize_t saved = fb_copy_file_range(fd_in, &off_in, fd_, NULL, len, 0);
  if (saved == -1) {
    fb_perror("copy_file_range");
    abort();
  }
  assert_cmp(saved, ==, len);

  offset_ += len;
  assert_cmp(offset_, >, 0);
}

bool PipeRecorder::store(bool *is_empty_out, Hash *key_out, off_t* stored_bytes,
                         char **inline_data_out, size_t *inline_data_len_out) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "");

  assert(!deactivated_);
  assert(!abandoned_);

  *inline_data_out = NULL;
  *inline_data_len_out = 0;

  bool ret;
  if (use_memory_buffer_ && offset_ > 0) {
    /* Data is in memory buffer - compute hash and return inline data */
    FB_DEBUG(FB_DEBUG_CACHING, "PipeRecorder: returning inline data, len=" + d(offset_));
    *is_empty_out = false;
    /* Key_out is not set. */
    /* Keep the buffer until recorder is deleted. */
    *inline_data_out = mem_buffer_;
    *inline_data_len_out = offset_;
    ret = true;
  } else if (fd_ >= 0) {
    /* Some data was seen and written to file. Place it in the blob cache, get its hash. */
    FB_DEBUG(FB_DEBUG_CACHING, "PipeRecorder: storing to blob cache, offset=" + d(offset_));
    *is_empty_out = false;
    ret = blob_cache->move_store_file(filename_, fd_, offset_, key_out);
    /* Note: move_store_file() closed the fd_. */
    fd_ = -1;
    *stored_bytes = ret ? offset_ : 0;
  } else {
    /* No data was seen at all. */
    *is_empty_out = true;
    ret = true;
  }
  free(filename_);
  filename_ = NULL;
  abandoned_ = true;
  return ret;
}

void PipeRecorder::abandon() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "");

  assert(!abandoned_);

  if (fd_ >= 0) {
    close(fd_);
    unlink(filename_);
    fd_ = -1;
  }
  free(filename_);
  filename_ = NULL;
  if (mem_buffer_) {
    free(mem_buffer_);
    mem_buffer_ = NULL;
    mem_buffer_capacity_ = 0;
  }
  abandoned_ = true;
}

void PipeRecorder::deactivate() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "");

  assert(!deactivated_);

  if (fd_ >= 0) {
    close(fd_);
    unlink(filename_);
    fd_ = -1;
  }
  free(filename_);
  filename_ = NULL;
  if (mem_buffer_) {
    free(mem_buffer_);
    mem_buffer_ = NULL;
    mem_buffer_capacity_ = 0;
  }
  deactivated_ = true;
}

bool PipeRecorder::has_active_recorder(
    const std::vector<std::shared_ptr<PipeRecorder>>& recorders) {
  for (size_t i = 0; i < recorders.size(); i++) {
    if (!recorders[i]->deactivated_) {
      return true;
    }
  }
  return false;
}

void PipeRecorder::record_data_from_buffer(std::vector<std::shared_ptr<PipeRecorder>> *recorders,
                                           const char *buf, ssize_t len) {
  TRACK(FB_DEBUG_PIPE, "#recorders=%" PRIsize ", len=%" PRIssize, recorders->size(), len);

  assert_cmp(len, >, 0);

  // FIXME Would it be faster to call add_data_from_buffer() for the first active recorder only,
  // and then do add_data_from_regular_fd() (i.e. copy_file_range()) for the rest?
  for (std::shared_ptr<PipeRecorder>& recorder : *recorders) {
    if (!recorder->deactivated_) {
      recorder->add_data_from_buffer(buf, len);
    }
  }
}

void PipeRecorder::record_data_from_unix_pipe(std::vector<std::shared_ptr<PipeRecorder>> *recorders,
                                              int fd, ssize_t len) {
  TRACK(FB_DEBUG_PIPE, "#recorders=%" PRIsize ", fd=%d, len=%" PRIssize,
        recorders->size(), fd, len);

#ifdef FB_EXTRA_DEBUG
  assert(has_active_recorder(*recorders));
#endif
  assert_cmp(len, >, 0);

  /* The first active recorder consumes the data from the pipe. */
  size_t i;
  for (i = 0; i < recorders->size(); i++) {
    if (!(*recorders)[i]->deactivated_) {
      (*recorders)[i]->add_data_from_unix_pipe(fd, len);
      break;
    }
  }

  size_t first_active = i++;
  loff_t first_active_offset = (*recorders)[first_active]->offset_;
  char* first_active_mem_buffer = (*recorders)[first_active]->mem_buffer_;
  int first_active_fd = (*recorders)[first_active]->fd_;
  /* The remaining active recorders copy from the first one's backing file. */
  for (; i < recorders->size(); i++) {
    if (!(*recorders)[i]->deactivated_) {
      if ((*recorders)[first_active]->use_memory_buffer_) {
        (*recorders)[i]->add_data_from_buffer(first_active_mem_buffer + first_active_offset - len ,
                                              len);
      } else {
        (*recorders)[i]->add_data_from_regular_fd(first_active_fd, first_active_offset - len, len);
      }
    }
  }
}

void PipeRecorder::record_data_from_regular_fd(
    std::vector<std::shared_ptr<PipeRecorder>> *recorders,
    int fd, ssize_t len) {
  TRACK(FB_DEBUG_PIPE, "#recorders=%" PRIsize ", fd=%d, len=%" PRIssize,
        recorders->size(), fd, len);

  assert_cmp(len, >, 0);

  for (std::shared_ptr<PipeRecorder>& recorder : *recorders) {
    if (!recorder->deactivated_) {
      recorder->add_data_from_regular_fd(fd, 0, len);
    }
  }
}

void PipeRecorder::set_base_dir(const char *dir) {
  free(base_dir_);
  base_dir_ = strdup(dir);
  mkdir(base_dir_, 0700);
}

std::string PipeRecorder::d_internal(const int level) const {
  (void)level;
  std::string ret = "{PipeRecorder #" + d(id_) + ", " + d(offset_) + " bytes";
  if (abandoned_) {
    ret += ", abandoned";
  } else if (deactivated_) {
    ret += ", deactivated";
  } else {
    ret += " so far";
  }
  ret += "}";
  return ret;
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const PipeRecorder& recorder, const int level) {
  return recorder.d_internal(level);
}
std::string d(const PipeRecorder *recorder, const int level) {
  if (recorder) {
    return d(*recorder, level);
  } else {
    return "{PipeRecorder NULL}";
  }
}

int PipeRecorder::id_counter_ = 0;
char *PipeRecorder::base_dir_ = NULL;

}  /* namespace firebuild */
