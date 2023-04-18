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
#include <unistd.h>

#include <cassert>

#include "common/firebuild_common.h"
#include "common/platform.h"
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

  if (fd_ < 0) {
    open_backing_file();
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

  if (fd_ < 0) {
    open_backing_file();
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

  if (fd_ < 0) {
    open_backing_file();
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

bool PipeRecorder::store(bool *is_empty_out, Hash *key_out, off_t* stored_bytes) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, PipeRecorder, this, "");

  assert(!deactivated_);
  assert(!abandoned_);

  bool ret;
  if (fd_ >= 0) {
    /* Some data was seen. Place it in the blob cache, get its hash. */
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
  /* The remaining active recorders copy from the first one's backing file. */
  for (; i < recorders->size(); i++) {
    if (!(*recorders)[i]->deactivated_) {
      (*recorders)[i]->add_data_from_regular_fd((*recorders)[first_active]->fd_,
                                                (*recorders)[first_active]->offset_ - len, len);
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
