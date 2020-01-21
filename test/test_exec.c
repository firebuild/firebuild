#include <unistd.h>

int main() {
  char * const argv[] = {"echo", "ok", NULL};
  execvp("foo", argv);
  execvp("echo", argv);
  return 0;
}
