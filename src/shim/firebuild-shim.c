/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Shim that runs argv[0] with FireBuild interception.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  char *executable = real_executable(argv[0]);
  execv(executable, argv);
  free(executable);
}
