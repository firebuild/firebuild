/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef COMMON_FIREBUILD_COMMON_H_
#define COMMON_FIREBUILD_COMMON_H_

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>

#include "common/cstring_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This structure's size needs to be a multiple of 8 bytes, so that reads from the serialized FBB
 * message, which follows this structure in memory, are properly aligned. */
typedef struct msg_header_ {
  /* message payload size (without the header or the attached fds), in bytes */
  uint32_t msg_size;
  /* ack_id, or 0 if unused */
  uint16_t ack_id;
  /* the number of fds attached as ancillary data (SCM_RIGHTS) */
  uint16_t fd_count;
} msg_header;

/**
 * cstring_view_array allows to conveniently build up an array of strings (i.e. NULL-terminated char**).
 */
typedef struct {
  cstring_view *p;
  int len;         /* excluding the trailing NULL */
  int size_alloc;  /* including the room for the trailing NULL */
} cstring_view_array;

void cstring_view_array_init(cstring_view_array *array);
void cstring_view_array_append(cstring_view_array *array, char *s);
void cstring_view_array_sort(cstring_view_array *array);
void cstring_view_array_deep_free(cstring_view_array *array);
bool is_cstring_view_array_full(cstring_view_array *array);
void cstring_view_array_append_noalloc(cstring_view_array *array, char *s);

#define STATIC_CSTRING_VIEW_ARRAY(name, size)       \
  cstring_view name##_ptrs[size] = {0};             \
  cstring_view_array name = {name##_ptrs, 0, size}

/**
 * voidp_array allows to conveniently build up an array of pointers (i.e. NULL-terminated void**).
 */
typedef struct {
  void **p;
  int len;         /* excluding the trailing NULL */
  int size_alloc;  /* including the room for the trailing NULL */
} voidp_array;

void voidp_array_init(voidp_array *array);
void voidp_array_append(voidp_array *array, void *p);
void voidp_array_deep_free(voidp_array *array, void (*fn_free)(void *));

/**
 * voidp_set allows to conveniently build up an unordered set of pointers.
 */
typedef struct {
  const void **p;
  int len;
  int size_alloc;
} voidp_set;

void voidp_set_init(voidp_set *set);
void voidp_set_clear(voidp_set *set);
bool voidp_set_contains(const voidp_set *set, const void *p);
void voidp_set_insert(voidp_set *set, const void *p);
void voidp_set_erase(voidp_set *set, const void *p);

bool is_path_at_locations(const char *path, const ssize_t len,
                          const cstring_view_array *prefix_array);

/**
 * Checks if the file name is canonical, i.e.:
 * - does not start with "./"
 * - does not end with "/" or "/."
 * - does not contain "//" or "/./"
 * - can contain "/../", since they might point elsewhere if a symlink led to its containing
 *    directory.
 *  See #401 for further details and gotchas.
 *
 * Returns if the path is in canonical form
 */
bool is_canonical(const char * const path, const size_t length);

static inline bool is_rdonly(int flags) { return ((flags & O_ACCMODE) == O_RDONLY); }
static inline bool is_wronly(int flags) { return ((flags & O_ACCMODE) == O_WRONLY); }
static inline bool is_rdwr(int flags)   { return ((flags & O_ACCMODE) == O_RDWR); }
// static inline bool is_read(int flags)   { return (is_rdonly(flags) || is_rdwr(flags)); }
static inline bool is_write(int flags)  { return (is_wronly(flags) || is_rdwr(flags)); }

/**
 * wrapper for read() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_read(int fd, void *buf, size_t count);

/**
 * wrapper for write() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_write(int fd, const void *buf, size_t count);

/**
 * wrapper for writev() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}  /* extern "C" */
#endif

/** Wrapper macro for read() or write() retrying on recoverable errors
 *  (EINTR and short read/write). */
#define FB_READ_WRITE(op, fd, buf, count)                               \
  {                                                                     \
    ssize_t ret;                                                        \
    size_t remaining = count;                                           \
    do {                                                                \
      ret = (op)(fd, buf, remaining);                                   \
      if (ret == -1) {                                                  \
        if (errno == EINTR) {                                           \
          continue;                                                     \
        } else {                                                        \
          return ret;                                                   \
        }                                                               \
      } else if (ret == 0) {                                            \
        return ret;                                                     \
      } else {                                                          \
        remaining -= ret;                                               \
        buf = ((char *) buf) + ret;                                     \
      }                                                                 \
    } while (remaining > 0);                                            \
    return count;                                                       \
  }

/**
 * Wrapper macro for readv() or writev(), with the following differences:
 *
 * - retries/continues on recoverable errors (EINTR and short read/write);
 *
 * - iov/iovcnt can be arbitrarily large (if it's larger than IOV_MAX then
 *   the operation will not be atomic though);
 *
 * - in order to implement the previous two without having to copy the
 *   entire iov array, iov isn't const.
 */
#define FB_READV_WRITEV(op, fd, iov, iovcnt)                            \
  {                                                                     \
    ssize_t ret;                                                        \
    ssize_t written = 0;                                                \
    ssize_t remaining = 0;                                              \
    for (int i = 0; i < iovcnt; i++) {                                  \
      remaining += iov[i].iov_len;                                      \
    }                                                                   \
    while (1) {                                                         \
      ret = (op)(fd, iov, iovcnt <= IOV_MAX ? iovcnt : IOV_MAX);        \
      if (ret == remaining) {                                           \
        /* completed */                                                 \
        return written + ret;                                           \
      } else if (ret == -1) {                                           \
        if (errno == EINTR) {                                           \
          /* retry on signal */                                         \
          continue;                                                     \
        } else {                                                        \
          /* bail out on permanent error */                             \
          return ret;                                                   \
        }                                                               \
      } else {                                                          \
        /* some, but not all bytes have been written */                 \
        written += ret;                                                 \
        remaining -= ret;                                               \
        while ((size_t) ret >= iov[0].iov_len) {                        \
          /* skip the fully written chunks */                           \
          ret -= iov[0].iov_len;                                        \
          iov++;                                                        \
          iovcnt--;                                                     \
        }                                                               \
        /* adjust the partially written chunk */                        \
        iov[0].iov_base = ((char *) iov[0].iov_base) + ret;             \
        iov[0].iov_len -= ret;                                          \
      }                                                                 \
    }                                                                   \
  }

#endif  // COMMON_FIREBUILD_COMMON_H_
