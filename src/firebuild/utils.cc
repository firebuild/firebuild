/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include <errno.h>
#include <linux/aio_abi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fmt/core.h>
#if FMT_VERSION > 70000
#include <fmt/compile.h>
#else
#define FMT_COMPILE FMT_STRING
#endif
#include <fmt/format.h>
#include <string>
#include <cstdlib>
#include <unordered_map>

#include "./fbbcomm.h"
#include "common/firebuild_common.h"
#include "firebuild/debug.h"

msg_header fixed_ack_msg = {0, 0};
aio_context_t io_ctx = 0;
io_event io_events[500];

std::unordered_map<int, struct iocb*>* ack_iocbs = nullptr;

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(write, fd, buf, count);
}

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags) {
  ssize_t ret;
  size_t remaining = len;
  do {
    ret = copy_file_range(fd_in, off_in, fd_out, off_out, remaining, flags);
    if (ret == -1) {
      if (errno == EXDEV) {
        perror("copy_file_range");
        // TODO(rbalint) fall back to fb_read and fb_write
        assert(0 && "cache and system or build area on different mount points is supported only "
               "with Linux 5.3 and later");
      } else {
        return ret;
      }
    } else if (ret == 0) {
      return len - remaining;
    } else {
      remaining -= ret;
    }
  } while (remaining > 0);
  return len;
}

namespace firebuild {

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const uint32_t ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d", D_FD(conn), ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
#ifdef NDEBUG
  if (!ack_iocbs) {
    ack_iocbs = new std::unordered_map<int, struct iocb*>();
    syscall(__NR_io_setup, 500, &io_ctx);
  }
  iocb* control_block;
  auto it = ack_iocbs->find(conn);
  if (it != ack_iocbs->end()) {
    control_block = it->second;
  } else {
    control_block = new iocb();
    (*ack_iocbs)[conn] = control_block;
  }
  memset(control_block, 0, sizeof(iocb));
  control_block->aio_fildes = conn;
  control_block->aio_buf = reinterpret_cast<__u64>(&fixed_ack_msg);
  control_block->aio_nbytes = sizeof(fixed_ack_msg);
  control_block->aio_lio_opcode = IOCB_CMD_PWRITE;
  int io_submit_ret = syscall(__NR_io_submit, io_ctx, 1, &control_block);
  if (io_submit_ret == -1) {
    if (errno == EAGAIN) {
      /* Flush prior results */
      syscall(__NR_io_getevents, io_ctx, 0, 500, &io_events, NULL);
      syscall(__NR_io_submit, io_ctx, 1, &control_block);
    } else {
      perror("io_submit");
      abort();
    }
  }
#else
  msg_header msg = {.msg_size = 0, .ack_id = ack_num};
  fb_write(conn, &msg, sizeof(msg));
#endif
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

std::string make_fifo(int fd, int flags, int pid, const char* fb_conn_string,
                      int* fifo_name_offset) {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  std::string fifo_params_fd_flags = fmt::format(FMT_COMPILE("{}: {} "), fd, flags);
  *fifo_name_offset = fifo_params_fd_flags.length();
  std::string fifo_params = fmt::format(FMT_COMPILE("{}{}-{}-{}-{:09d}-{:09d})"),
                                        fifo_params_fd_flags,
                                        fb_conn_string, pid, fd, time.tv_sec, time.tv_nsec);
  const char* fifo = fifo_params.c_str() + *fifo_name_offset;
  int ret = mkfifo(fifo, 0666);
  if (ret == -1) {
    perror("could not create fifo");
    return nullptr;
  }
  return fifo_params;
}

}  // namespace firebuild
