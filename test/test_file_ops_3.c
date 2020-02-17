#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
  int fd;

  /* Open existing file for reading. */
  fd = open("/etc/passwd", O_RDONLY);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  /* Attempt to open nonexisting file for reading. */
  fd = open("/no/such/file", O_RDONLY);
  if (fd != -1) {
    fprintf(stderr, "open should have failed\n");
    exit(1);
  }
  close(fd);

  /* Attempt to write to nonexisting file, without O_CREAT. */
  fd = open("wont_create_1", O_WRONLY|O_TRUNC);
  if (fd != -1) {
    fprintf(stderr, "open should have failed\n");
    exit(1);
  }

  /* Attempt to write to existing file, with O_EXCL. */
  fd = open("test_empty_1.txt", O_WRONLY|O_CREAT|O_EXCL, 0600);
  if (fd != -1) {
    fprintf(stderr, "open should have failed\n");
    exit(1);
  }

  /* Open for writing, but don't modify. */
  fd = open("test_nonempty_1.txt", O_WRONLY);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  /* Open for writing, truncate. */
  fd = open("test_nonempty_2.txt", O_WRONLY|O_TRUNC);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  /* Open for writing existing file, with CREAT and TRUNC. */
  fd = creat("test_maybe_exists_1.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  /* Open for writing nonexisting file, with CREAT and TRUNC. */
  fd = creat("test_maybe_exists_2.txt", 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  /* Exclusive creation. */
  fd = open("test_exclusive.txt", O_WRONLY|O_CREAT|O_EXCL, 0600);
  if (fd == -1) {
    perror("open");
    exit(1);
  }
  close(fd);

  return 0;
}
