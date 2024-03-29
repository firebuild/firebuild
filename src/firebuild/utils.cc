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

#include "firebuild/utils.h"

#include <dirent.h>
#include <errno.h>
#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#include <plist/plist++.h>
#endif
#include <stdio.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "./fbbcomm.h"
#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/debug.h"

#ifdef __APPLE__
/* Interesting CSR configuration flags. */
#define CSR_ALLOW_UNRESTRICTED_FS    (1 << 1)
#define CSR_ALLOW_TASK_FOR_PID       (1 << 2)
#ifdef __aarch64__
#define CSR_ALLOW_UNRESTRICTED_NVRAM (1 << 6)
#endif
typedef uint32_t csr_config_t;
extern "C" {
extern int csr_check(csr_config_t);
}
#endif

std::unordered_set<std::string>* deduplicated_strings = nullptr;

ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(write, fd, buf, count);
}

ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}

ssize_t fb_read(int fd, void *buf, size_t count) {
  FB_READ_WRITE(read, fd, buf, count);
}

ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, const size_t len,
                           unsigned int flags) {
  ssize_t ret;
  size_t remaining = len;
  do {
#ifdef __APPLE__
    ret = -1;
    errno = ENOSYS;
    (void)flags;
#else
    ret = copy_file_range(fd_in, off_in, fd_out, off_out, remaining, flags);
#endif
    if (ret == -1) {
      if (errno == EXDEV || errno == ENOSYS) {
        /* Fall back to read and write. */
        const bool do_malloc = remaining > 64 * 1024;
        void* buf = do_malloc ? malloc(remaining) : alloca(remaining);
        ssize_t bytes_read, bytes_written;
        if (off_in) {
          const off_t start_pos = lseek(fd_in, 0, SEEK_CUR);
          ret = lseek(fd_in, *off_in, SEEK_SET);
          assert_cmp(ret, ==, *off_in);
          bytes_read = fb_read(fd_in, buf, remaining);
          *off_in += bytes_read;
          ret = lseek(fd_in, start_pos, SEEK_SET);
          assert_cmp(ret, ==, start_pos);
        } else {
          bytes_read = fb_read(fd_in, buf, remaining);
        }
        if (off_out) {
          const off_t start_pos = lseek(fd_out, 0, SEEK_CUR);
          ret = lseek(fd_out, *off_out, SEEK_SET);
          assert_cmp(ret, ==, *off_out);
          bytes_written = fb_write(fd_out, buf, bytes_read);
          *off_out += bytes_written;
          ret = lseek(fd_out, start_pos, SEEK_SET);
          assert_cmp(ret, ==, start_pos);
        } else {
          bytes_written = fb_write(fd_out, buf, bytes_read);
        }
        if (do_malloc) {
          free(buf);
        }
        return bytes_written;
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

unsigned char fixed_dirent_type(const struct dirent* dirent, DIR* dir,
                                const std::string& dir_path) {
  if (dirent->d_type == DT_UNKNOWN) {
    struct stat st;
    if (fstatat(dirfd(dir), dirent->d_name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
      firebuild::fb_error("Failed checking stat()-ing file: " +
                          dir_path + "/" + firebuild::d(dirent->d_name));
      perror("fstatat");
      return dirent->d_type;
    } else {
      switch (st.st_mode & S_IFMT) {
        case S_IFREG:
          return DT_REG;
        case S_IFDIR:
          return DT_DIR;
        default:
          /* Leaving d_type as it was. */
          return DT_UNKNOWN;
      }
    }
  } else {
    return dirent->d_type;
  }
}

off_t file_size(DIR* dir, const char* name) {
  struct stat st;
  int dir_fd = dir ? dirfd(dir) : AT_FDCWD;
  if (fstatat(dir_fd, name, &st, 0) == 0) {
    return S_ISREG(st.st_mode) ? st.st_size : 0;
  } else {
    firebuild::fb_perror("fstatat");
    return 0;
  }
}

off_t recursive_total_file_size(const std::string& path) {
  DIR * dir = opendir(path.c_str());
  if (dir == NULL) {
    return 0;
  }

  /* Visit dirs recursively and collect all the file sizes. */
  off_t total = 0;
  struct dirent *dirent;
  while ((dirent = readdir(dir)) != NULL) {
    const char* name = dirent->d_name;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue;
    }
    switch (fixed_dirent_type(dirent, dir, path)) {
      case DT_DIR: {
        total += recursive_total_file_size(path + "/" + name);
        break;
      }
      case DT_REG: {
        struct stat st;
        if (fstatat(dirfd(dir), name, &st, 0) == 0) {
          total += st.st_size;
        }
        break;
      }
      default:
        /* Just ignore the non-regulat file. */
        break;
    }
  }
  closedir(dir);
  return total;
}


int file_overwrite_printf(const std::string& path, const char* format, ...) {
  int ret;
  FILE* f;
  const std::string tmp_path = path + "." + std::to_string(getpid());
  if (!(f = fopen(tmp_path.c_str(), "w"))) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }
  va_list ap;
  va_start(ap, format);
  ret = vfprintf(f, format, ap);
  if (ret == -1) {
    perror("vfprintf");
    unlink(tmp_path.c_str());
    return ret;
  }
  va_end(ap);
  fclose(f);
  ret = rename(tmp_path.c_str(), path.c_str());
  if (ret < 0) {
    unlink(tmp_path.c_str());
  }
  return ret;
}

void bump_limits() {
  struct rlimit rlim;
  getrlimit(RLIMIT_NOFILE, &rlim);
  /* 8K is expected to be enough for up more than 2K parallel intercepted processes, thus try to
   * bump the limit above that. */
  rlim_t preferred_limit = (rlim.rlim_max == RLIM_INFINITY) ? 8192 : rlim.rlim_max;
  if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < preferred_limit) {
    FB_DEBUG(firebuild::FB_DEBUG_COMM, "Increasing limit of open files from "
             + std::to_string(rlim.rlim_cur) + " to " + std::to_string(preferred_limit) + "");
    rlim.rlim_cur = preferred_limit;
    setrlimit(RLIMIT_NOFILE, &rlim);
  }
}

namespace firebuild {

void ack_msg(const int conn, const uint16_t ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d", D_FD(conn), ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
  msg_header msg = {};
  msg.ack_id = ack_num;
  fb_write(conn, &msg, sizeof(msg));
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

void send_fbb(int conn, int ack_num, const FBBCOMM_Builder *msg, int *fds, int fd_count) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d fd_count=%d", D_FD(conn), ack_num, fd_count);

  if (FB_DEBUGGING(firebuild::FB_DEBUG_COMM)) {
    std::vector<int> fds_vec(fds, fds + fd_count);
    fprintf(stderr, "Sending message with ancillary fds %s:\n", D(fds_vec));
    msg->debug(stderr);
  }

  int len = msg->measure();

  char *buf = reinterpret_cast<char *>(alloca(sizeof(msg_header) + len));
  memset(buf, 0, sizeof(msg_header));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
  reinterpret_cast<msg_header *>(buf)->ack_id = ack_num;
  reinterpret_cast<msg_header *>(buf)->msg_size = len;
  reinterpret_cast<msg_header *>(buf)->fd_count = fd_count;
#pragma GCC diagnostic pop

  msg->serialize(buf + sizeof(msg_header));

  if (fd_count == 0) {
    /* No fds to attach. Send the header and the payload in a single step. */
    fb_write(conn, buf, sizeof(msg_header) + len);
  } else {
    /* We have some fds to attach. Send the header and the payload separately. This means that the
     * file descriptors (ancillary data) are attached to the first byte of the payload. */

    /* Send the header. */
    fb_write(conn, buf, sizeof(msg_header));

    /* Prepare to send the payload, with the fds attached as ancillary data. */
    struct iovec iov = {};
    iov.iov_base = buf + sizeof(msg_header);
    iov.iov_len = len;

    void *anc_buf;
    size_t anc_buf_size = CMSG_SPACE(fd_count * sizeof(int));
    anc_buf = alloca(anc_buf_size);
    memset(anc_buf, 0, anc_buf_size);

    struct msghdr msgh = {};
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = anc_buf;
    msgh.msg_controllen = anc_buf_size;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(fd_count * sizeof(int));
    memcpy(CMSG_DATA(cmsg), fds, fd_count * sizeof(int));

    /* Send the payload. The socket is almost empty (it can only contain the header), so we can
     * safely expect sendmsg() to fully succeed, no short write, if the message is reasonably sized.
     * FIXME implement fb_sendmsg() which retries, just to be even safer. */
    sendmsg(conn, &msgh, 0);
  }
}

void fb_perror(const char *s) {
  perror((std::string("FIREBUILD: ") + s).c_str());
}
#
int fb_renameat2(int olddirfd, const char *oldpath,
                 int newdirfd, const char *newpath, unsigned int flags) {
  int ret;
#if FB_GLIBC_PREREQ(2, 28)
  ret = renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
#else
#ifdef SYS_renameat2
  ret = syscall(SYS_renameat2, olddirfd, oldpath, newdirfd, newpath, flags);
#else
  ret = -1;
  errno = ENOSYS;
  (void)flags;
#endif
#endif
  if (ret == -1 && (errno == ENOSYS || errno == EINVAL)) {
    if (flags & RENAME_NOREPLACE && faccessat(newdirfd, newpath, F_OK, 0) == 0) {
      errno = EEXIST;
      return -1;
    } else {
      return renameat(olddirfd, oldpath, newdirfd, newpath);
    }
  } else {
    return ret;
  }
}

const std::string& deduplicated_string(std::string str) {
  if (!deduplicated_strings) {
    deduplicated_strings = new std::unordered_set<std::string>();
  }
  return *deduplicated_strings->insert(str).first;
}

bool check_system_setup() {
  bool system_ok = true;
#ifdef __APPLE__
  /* Check SIP. */
  if (csr_check(CSR_ALLOW_UNRESTRICTED_FS
#ifdef __aarch64__
                | CSR_ALLOW_UNRESTRICTED_NVRAM
#endif
                | CSR_ALLOW_TASK_FOR_PID) != 0) {
    fb_info("System Integrity Protection prevents intercepting the BUILD COMMAND.");
    system_ok = false;
  }

  /* Check Library Validation. */
  plist_t plist = nullptr, disable_library_validation = nullptr;
  if (plist_read_from_file("/Library/Preferences/com.apple.security.libraryvalidation.plist",
                           &plist, nullptr) != PLIST_ERR_SUCCESS
      || !(disable_library_validation = plist_dict_get_item(plist, "DisableLibraryValidation"))
      || !(plist_get_node_type(disable_library_validation) == PLIST_BOOLEAN)
      || !plist_bool_val_is_true(disable_library_validation)) {
    fb_info("Library Validation is enabled possibly preventing interception of Xcode and other "
            "protected tools.");
    system_ok = false;
  }
  if (plist) {
    plist_free(plist);
  }

#ifdef __aarch64__
  /* Check if nvram's boot-args contains -arm64e_preview_abi. */
  io_registry_entry_t options = IORegistryEntryFromPath(kIOMainPortDefault,
                                                        "IODeviceTree:/options");
  if (options) {
    CFTypeRef bootArgsRef = IORegistryEntryCreateCFProperty(options, CFSTR("boot-args"),
                                                            kCFAllocatorDefault, 0);
    if (bootArgsRef) {
      CFStringRef bootArgs = (CFStringRef)bootArgsRef;
      if (CFStringFind(bootArgs, CFSTR("-arm64e_preview_abi"),
                       kCFCompareCaseInsensitive).location == kCFNotFound) {
        fb_info("The system is not configured to use the arm64e_preview_abi, which is needed "
                "for intercepting arm64e binaries.");
        system_ok = false;
      }
      CFRelease(bootArgsRef);
    }
    IOObjectRelease(options);
  }
#endif

  if (!system_ok) {
    fb_info("Visit https://firebuild.com/setup-macos for guidelines for setting up your system.");
  }
#endif
  return system_ok;
}

}  /* namespace firebuild */
