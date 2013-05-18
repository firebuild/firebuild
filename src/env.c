
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

void get_argv_env(char *** argv, char ***env)
{
  char* arg = *(__environ - 2);
  unsigned long int argc_guess = 0;

  /* argv is NULL terminated */
  assert(*(__environ - 1) == NULL);
  /* walk back on argv[] to find the first value matching the counted argument number */
  while (argc_guess != (unsigned long int)arg) {
    argc_guess++;
    arg = *(__environ - 2 - argc_guess);
  }
  
  *argv = __environ - 1 - argc_guess;
  *env = __environ;
}

// TODO for valgrind
// void free_arc_argv_env()
