/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Shim that runs argv[0] with FireBuild interception.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIBFIREBUILD_SO_LEN strlen(LIBFIREBUILD_SO)
#define MAX_PID_STR_LEN 32


static void usage() {
  printf("Helper binary for FireBuild™.\n"
         "Create symlinks to this binary in the \"intercepted_commads_dir\" directory set\n"
         "in FireBuild™'s configuration file.\n\n"
         "Don't run this binary directly. It is useful only when it is ran by in a build\n"
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
} inode_fd;

static int cmp_inode_fds(const void *p1, const void *p2) {
  ino_t inode1 = ((inode_fd*)p1)->inode;
  ino_t inode2 = ((inode_fd*)p2)->inode;
  if (inode1 == inode2) {
    int fd1 = ((inode_fd*)p1)->fd;
    int fd2 = ((inode_fd*)p2)->fd;
    return (fd1 == fd2) ? 0 : (fd1 < fd2 ? -1 : 1);
  } else {
    return inode1 < inode2 ? -1 : 1;
  }
}

/**
 * List of open file descriptors grouped by pointing to the same inode. The groups are separated
 * by ":", the fd-s are separated by ",", e.g.: "1,2:3", where STDERR and STDOUT are pointing to
 * the same inode, and fd 3 is a separate one.
 * @param fd_dir "/proc/self/fd" or "/proc/NNN/fd", where NNN is the pid
 */
char* fd_map(const char * fd_dir) {
  size_t ret_buf_size = 32, ret_len = 0;
  char *ret_buf = malloc(ret_buf_size);
  ret_buf[0] = '\0';
  size_t inode_fds_buf_size = 4096;
  size_t inode_fds_size = 0;
  inode_fd *inode_fds = malloc(sizeof(inode_fd) * inode_fds_buf_size);
  DIR *dir = opendir(fd_dir);
  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    if (de->d_type == DT_LNK) {
      int fd_num = atoi(de->d_name);
      if (fd_num == dirfd(dir)) {
        continue;
      }
      int acc_mode = fcntl(fd_num, F_GETFL) & O_ACCMODE;
      if (acc_mode != -1) {
        struct stat statbuf;
        if (fstatat(dirfd(dir), de->d_name, &statbuf, 0) != -1) {
          if (inode_fds_size == inode_fds_buf_size) {
            inode_fds_buf_size *= 2;
            inode_fds = realloc(inode_fds, inode_fds_buf_size);
          }
          inode_fds[inode_fds_size].inode = statbuf.st_ino;
          inode_fds[inode_fds_size].acc_mode = acc_mode;
          inode_fds[inode_fds_size++].fd = fd_num;
        }
      }
    }
  }
  closedir(dir);
  if (inode_fds_size == 0) {
    free(inode_fds);
    return ret_buf;
  }
  qsort(inode_fds, inode_fds_size, sizeof(inode_fd), cmp_inode_fds);

  ino_t last_inode = inode_fds[0].inode + 1;
  for (size_t i = 0; i < inode_fds_size; i++) {
    if (ret_buf_size - ret_len < 32) {
      ret_buf_size *= 2;
      ret_buf = realloc(ret_buf, ret_buf_size);
    }
    ret_len += snprintf(&ret_buf[ret_len], ret_buf_size - ret_len, "%s%d=%d",
                        (last_inode == inode_fds[i].inode) ? "," : ((ret_len > 0) ? ":" : ""),
                        inode_fds[i].fd, inode_fds[i].acc_mode);
    last_inode = inode_fds[i].inode;
  }
  free(inode_fds);
  return ret_buf;
}

void export_fd_map() {
  char *fds = fd_map("/proc/self/fd");
  setenv("FIREBUILD_SHIM_FDS", fds, 0);
  free(fds);
}

/**
 * Find the first executable with the same base name that's not this binary.
 */
static char * real_executable(const char *argv0) {
  const char *base_name = strrchr(argv0, '/');
  if (!base_name) {
    base_name = argv0;
  } else {
    base_name++;
  }
  const size_t base_len = strlen(base_name);

  char *tmp_path = strdup(getenv("PATH"));
  const size_t buf_size = strlen(tmp_path) + 2 + strlen(base_name);
  char* buf = malloc(buf_size);
  char *self = realpath("/proc/self/exe", NULL);
  char *strtok_r_saveptr;
  for (char *curr = strtok_r(tmp_path, ":", &strtok_r_saveptr);
       curr != NULL;
       curr = strtok_r(NULL, ":", &strtok_r_saveptr)) {
    const size_t curr_len = strlen(curr);
    memcpy(buf, curr, curr_len);
    buf[curr_len] = '/';
    memcpy(&buf[curr_len + 1], base_name, base_len + 1);
    char *candidate = realpath(buf, NULL);
    if (!candidate) {
      continue;
    } else {
      if (strcmp(self, candidate) != 0) {
        /* Found a different binary on the path, this should be the real executable. */
        free(tmp_path);
        free(buf);
        return candidate;
      }
      free(candidate);
    }
  }
  free(tmp_path);
  free(buf);
  char* self_base_name = strrchr(self, '/') + 1;
  if (strcmp(base_name, self_base_name) != 0) {
    fprintf(stderr, "ERROR: %s could not find the real \"%s\" executable on the PATH.\n",
            self_base_name, argv0);
  }
  usage();
  exit(1);
}

void export_shim_pid() {
  char pid_str[MAX_PID_STR_LEN];
  snprintf(pid_str, MAX_PID_STR_LEN, "%d", getpid());
  setenv("FIREBUILD_SHIM_PID", pid_str, 0);
}

int main(const int argc, char *argv[]) {
  (void)argc;
  if (!getenv("FB_SOCKET")) {
    fprintf(stderr, "ERROR: FB_SOCKET is not set, maybe firebuild is not running?\n");
    usage();
    exit(1);
  }
  fix_ld_preload();
  export_shim_pid();
  export_fd_map();
  char *executable = real_executable(argv[0]);
  execv(executable, argv);
  free(executable);
}
