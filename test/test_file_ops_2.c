#include <stdlib.h>
#include <unistd.h>

int main() {
  /*
   * Just run test_file_ops_3.
   *
   * The point is to test that file events propagate upwards from
   * test_file_ops_3 to this test_file_ops_2 correctly.
   */
  execl("./test_file_ops_3", "test_file_ops", NULL);
  exit(1);
}
