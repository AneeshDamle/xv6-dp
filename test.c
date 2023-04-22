#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;
  int x = atoi(argv[1]);
  printf(1, "START TEST\n");
  for (i = 0; i < x; i++) {
    printf(1, "MALLOC (%d)\n", i);
    malloc(4096);
  }
  bspages();
  printf(1, "TEST PASSED OK\n");
  exit();
}
