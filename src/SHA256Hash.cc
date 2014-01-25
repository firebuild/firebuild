
#include "SHA256Hash.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <Debug.h>

static const int hash_bufsize = 4096;

namespace firebuild 
{

int SHA256Hash::update(const std::string &from_path) {
  int fd;

  fd = open(from_path.c_str(), O_RDONLY);
  if(-1 == fd) {
    if (debug_level >= 3) {
      FB_DEBUG(3, "File " + from_path);
      perror("open");
    }
    close(fd);
    return -1;
  }

  struct stat64 st;
  if (-1 == fstat64(fd, &st)) {
    perror("fstat");
    close(fd);
    return -1;
  } else if (!S_ISREG(st.st_mode)) {
    // Only regular files' hash can be collected
    // TODO debug
    close(fd);
    return -1;
  }

  char buf[hash_bufsize];
  ssize_t bytes_read;
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  while (0 != (bytes_read = read(fd, buf, hash_bufsize))){
      if (-1 == bytes_read) {
        if (errno == EINTR) {
          continue;
        } else {
          perror("read");
          close(fd);
          return -1;
        }
      }
      SHA256_Update(&sha256, buf, bytes_read);
    }
  SHA256_Final(arr, &sha256);
  close(fd);
  return true;
}

}
