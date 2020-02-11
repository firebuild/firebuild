#include <unistd.h>

int main (int argc, char* argv[]) {
  int i;
  for (i = 3; i < 120; i++) {
    close(i);
  }
  execvp(argv[1], &argv[1]);
}
