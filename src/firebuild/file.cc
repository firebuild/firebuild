/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/file.h"

#include <libgen.h>
#include <sys/stat.h>

#include <cstring>

namespace firebuild {

File::File(const std::string &p)
    :mtimes_(), path_(p), exists_(false), hash_() {
}


int File::set_hash() {
  return hash_.set_from_file(path_, NULL);
}

int File::update() {
  if (!this->set_hash()) {
    return -1;
  }

  unsigned int i = 0;
  struct stat s;
  char *tmp_path = strdup(path_.c_str());
  if (-1 == lstat(tmp_path, &s)) {
    perror("lstat");
    return -1;
  } else {
    if (mtimes_.size() <= i) {
      mtimes_.resize(i+1);
    }
    mtimes_[i] = s.st_mtim;
  }
  // dirname may modify path and return dir pointing to a statically
  // allocated buffer. This is how we ended up having this complicated code
  char *dir;
  while (true) {
    i++;
    dir = dirname(tmp_path);
    /* XXX lstat is intercepted */
    if (-1 == lstat(dir, &s)) {
      perror("lstat");
      free(tmp_path);
      return -1;
    } else {
      if (mtimes_.size() <= i) {
        mtimes_.resize(i+1);
      }
      mtimes_[i] = s.st_mtim;
      // https://pubs.opengroup.org/onlinepubs/000095399/basedefs/xbd_chap04.html#tag_04_11
      // "A pathname that begins with two successive slashes may be interpreted in an implementation-defined manner [...]"
      if ((0 == strcmp(".", dir)) || (0 == strcmp("/", dir)) || (0 == strcmp("//", dir))) {
        break;
      } else {
        char * next_path = strdup(dir);
        free(tmp_path);
        tmp_path = next_path;
      }
    }
  }
  // we could resize the vector here, but the number of dirs in the path
  // can't change over time
  free(tmp_path);
  return 0;
}

#ifndef timespeccmp
#define timespeccmp(a, b, CMP)                                          \
  (((a)->tv_sec == (b)->tv_sec)?((a)->tv_nsec CMP(b)->tv_nsec):         \
   ((a)->tv_sec CMP(b)->tv_sec))
#endif

int File::is_changed() {
  int i = 0;
  char *tmp_path = strdup(path_.c_str());
  struct stat s;

  if (-1 == lstat(tmp_path, &s)) {
    perror("lstat");
    return -1;
  } else {
    if (!timespeccmp(&mtimes_[i], &s.st_mtim, ==)) {
      free(tmp_path);
      return 1;
    }
  }
  // dirname may modify path and return dir pointing to a statically
  // allocated buffer. This is how we ended up having this complicated code
  char *dir;
  while (true) {
    i++;
    dir = dirname(tmp_path);
    if (-1 == lstat(dir, &s)) {
      perror("lstat");
      free(tmp_path);
      return -1;
    } else {
      if (!timespeccmp(&mtimes_[i], &s.st_mtim, ==)) {
        free(tmp_path);
        return 1;
      }
      // https://pubs.opengroup.org/onlinepubs/000095399/basedefs/xbd_chap04.html#tag_04_11
      // "A pathname that begins with two successive slashes may be interpreted in an implementation-defined manner [...]"
      if ((0 == strcmp(".", dir)) || (0 == strcmp("/", dir)) || (0 == strcmp("//", dir))) {
        break;
      } else {
        char * next_path = strdup(dir);
        free(tmp_path);
        tmp_path = next_path;
      }
    }
  }
  // we could resize the vector here, but the number of dirs in the path
  // can't change over time
  free(tmp_path);
  return 0;
}

}  // namespace firebuild
