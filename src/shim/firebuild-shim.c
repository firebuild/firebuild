/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Shim that runs argv[0] with FireBuild interception.
 */

#include "shim/firebuild-shim.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "firebuild/platform.h"

#define LIBFIREBUILD_SO_LEN strlen(LIBFIREBUILD_SO)

static void usage() {
  printf("Helper binary for FireBuild™.\n"
         "Create symlinks to this binary in the \"intercepted_commands_dir\" directory set\n"
         "in FireBuild™'s configuration file.\n\n"
         "Don't run this binary directly. It is useful only when it is ran in a build\n"
         "intercepted by firebuild.\n");
}

static void fix_ld_preload() {
  const char *orig = getenv("LD_PRELOAD");
  if (!orig) {
    putenv("LD_PRELOAD=" LIBFIREBUILD_SO);
  } else if (!strstr(orig, LIBFIREBUILD_SO)) {
    const size_t orig_len = strlen(orig);
    char new_ld_preload[orig_len + LIBFIREBUILD_SO_LEN + 2];
    memcpy(new_ld_preload, LIBFIREBUILD_SO, LIBFIREBUILD_SO_LEN);
    new_ld_preload[LIBFIREBUILD_SO_LEN] = ':';
    memcpy(&new_ld_preload[LIBFIREBUILD_SO_LEN + 1], orig, orig_len + 1);
    setenv("LD_PRELOAD", new_ld_preload, 1 /* overwrite */);
  }
}

typedef struct inode_fd_ {
  ino_t inode;
  int fd;
  int acc_mode;
  int type_mode;
} inode_fd;

static int cmp_inode_fds(const void *p1, const void *p2) {
  ino_t inode1 = ((inode_fd*)p1)->inode;
  ino_t inode2 = ((inode_fd*)p2)->inode;
  if (inode1 == inode2) {
    int fd1 = ((inode_fd*)p1)->fd;
    int fd2 = ((inode_fd*)p2)->fd;
    return fdcmp(fd1, fd2);
  } else {
    return inode1 < inode2 ? -1 : 1;
  }
}

/**
 * List of open file descriptors grouped by pointing to the same open file description
 * (see kcmp(2)). The groups are separated by ":", the fd-s are separated by ",", e.g.: "1,2:3",
 * where STDERR and STDOUT are pointing to the same open file description, and fd 3 is a separate
 * one.
 * Each fd is described in "N=status=st_mode" form, where N is the fd number, acc_mode is the file
 * access mode as returned by fcntl(fd_num, F_GETFL) & O_ACCMODE, st_mode is stat.st_mode.
 *
 * @param fd_dir "/proc/self/fd" or "/proc/NNN/fd", where NNN is the pid
 * @param shim_fd socket to the supervisor, which should not be reported as an open fd
 * @param[out] fds_out array of open file descriptors ordered the same way as in the returned string
 * @param[out] fds_len_out number of open file descriptors
 * @return allocated string of fd descriptions, the caller should free it
 */
static char* get_fd_map(const char * fd_dir, int shim_fd, int **fds_out, int *fds_len_out) {
  size_t ret_buf_capacity = 32, ret_len = 0, fds_capacity = 8, fds_len = 0;
  char *ret_buf = malloc(ret_buf_capacity);
  ret_buf[0] = '\0';
  size_t inode_fds_capacity = fds_capacity;
  size_t inode_fds_len = 0;
  inode_fd *inode_fds = malloc(inode_fds_capacity * sizeof(inode_fd));
  int *fds = malloc(fds_capacity * sizeof(*fds_out)[0]);

  DIR *dir = opendir(fd_dir);
  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    if (de->d_type == DT_LNK) {
      int fd_num = atoi(de->d_name);
      if (fd_num == dirfd(dir) || fd_num == shim_fd) {
        continue;
      }
      const int status = fcntl(fd_num, F_GETFL);
      if (status != -1) {
        const int acc_mode = status & O_ACCMODE;
        struct stat statbuf;
        if (fstatat(dirfd(dir), de->d_name, &statbuf, 0) != -1) {
          if (inode_fds_len == inode_fds_capacity) {
            inode_fds_capacity *= 2;
            inode_fds = realloc(inode_fds, inode_fds_capacity * sizeof(inode_fd));
          }
          inode_fds[inode_fds_len].inode = statbuf.st_ino;
          inode_fds[inode_fds_len].acc_mode = acc_mode;
          inode_fds[inode_fds_len].type_mode = statbuf.st_mode;
          inode_fds[inode_fds_len++].fd = fd_num;
        }
      }
    }
  }
  closedir(dir);
  if (inode_fds_len == 0) {
    free(inode_fds);
    free(fds);
    *fds_out = NULL;
    *fds_len_out = 0;
    return ret_buf;
  }
  qsort(inode_fds, inode_fds_len, sizeof(inode_fd), cmp_inode_fds);

  ino_t last_inode = inode_fds[0].inode + 1;
  for (size_t i = 0; i < inode_fds_len; i++) {
    if (ret_buf_capacity - ret_len < 32) {
      ret_buf_capacity *= 2;
      ret_buf = realloc(ret_buf, ret_buf_capacity);
    }
    ret_len += snprintf(&ret_buf[ret_len], ret_buf_capacity - ret_len, "%s%d=%d=%d",
                        (last_inode == inode_fds[i].inode) ? "," : ((ret_len > 0) ? ":" : ""),
                        inode_fds[i].fd, inode_fds[i].acc_mode, inode_fds[i].type_mode);

    if (fds_len >= fds_capacity) {
      fds_capacity *= 2;
      fds = realloc(fds, fds_capacity * sizeof(fds[0]));
    }
    (fds)[fds_len++] = inode_fds[i].fd;
    last_inode = inode_fds[i].inode;
  }
  free(inode_fds);
  *fds_out = fds;
  *fds_len_out = fds_len;
  return ret_buf;
}

static void send_fds_to_supervisor(int shim_fd, pid_t pid, const int *fds, const int fd_count,
                            char *fd_map) {
  struct msghdr msg = {0};
  shim_msg_t shim_msg_head = {pid, fd_count, {}};
  struct cmsghdr *cmsg;
  struct iovec io[] = {{.iov_base = &shim_msg_head, .iov_len = sizeof(shim_msg_head)},
                       {.iov_base = fd_map, .iov_len = strlen(fd_map) + 1}};
  size_t buf_len = CMSG_SPACE(sizeof(fds[0]) * (fd_count + 1));
  int *buf = alloca(buf_len);
  int pipefd[2];

  memset(buf, 0, buf_len);
  msg.msg_iov = io;
  msg.msg_iovlen = 2;
  msg.msg_control = buf;
  msg.msg_controllen = buf_len;
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  /* we pass the underlying file descriptors instead of the raw numbers */
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fds[0]) * (fd_count + 1));
  if (fds) {
    memcpy(CMSG_DATA(cmsg), fds, sizeof(fds[0]) * fd_count);
  }

  if (pipe(pipefd) != 0) {
    perror("firebuild-shim: Failed to create pipe for supervisor communication");
    return;
  }
  fcntl(pipefd[0], F_SETFD, fcntl(pipefd[0], F_GETFD, 0) | FD_CLOEXEC);
  memcpy(CMSG_DATA(cmsg) + sizeof(fds[0]) * fd_count, &(pipefd[1]), sizeof(pipefd[1]));

  if (sendmsg(shim_fd, &msg, 0) < 0) {
    fprintf(stderr, "Failed to send fds to firebuild\n");
  }
  /* Wait for the supervisor to close the last fd to avoid potential race condition with the exec
     child that could connect the supervisor earlier. */
  close(pipefd[1]);
  if (read(pipefd[0], buf, 1) == -1) {
    perror("firebuild-shim: Failed to read from supervisor");
  }
}

int main(const int argc, char *argv[]) {
  (void)argc;

  if (!(getenv("FB_SOCKET"))) {
    fprintf(stderr, "ERROR: FB_SOCKET is not set, maybe firebuild is not running?\n");
    usage();
    exit(1);
  }
  const char* shim_fd_str = getenv("FIREBUILD_SHIM_FD");
  if (shim_fd_str) {
    int shim_fd = atoi(shim_fd_str);
    pid_t pid = getpid();
    if (shim_fd <= 0) {
      fprintf(stderr, "ERROR: FIREBUILD_SHIM_FD=%s is invalid\n", shim_fd_str);
      usage();
      exit(1);
    }
    fix_ld_preload();
    int *fds, fd_count;
    char *fd_map = get_fd_map("/proc/self/fd", shim_fd, &fds, &fd_count);
    send_fds_to_supervisor(shim_fd, pid, fds, fd_count, fd_map);
    free(fds);
    free(fd_map);
    unsetenv("FIREBUILD_SHIM_FD");
  } else {
    fprintf(stderr, "ERROR: FIREBUILD_SHIM_FD is not set, maybe firebuild is not running?\n");
    usage();
    exit(1);
  }
  setenv("PATH", strchr(getenv("PATH"), ':') + 1, 1);
  const char *base_name = strrchr(argv[0], '/');
  if (!base_name) {
    execvp(argv[0], argv);
  } else {
    /* Shim was executed using an (absolute?) path, use just the base name to find the real
     * executable on the PATH without the shim directory. */
    execvp(base_name + 1, argv);
  }
}
