#include "test.h"

void *fn1(void *p, int x, int y) { return p; }
void *fn2(int x, void *p, int y) { return p; }

int main() {
  char *p1 = alloca(16);
  char *p2 = alloca(16);
  char *p3 = 1 + (char *)alloca(3) + 1;
  p3 -= 2;
  void *p4 = fn1(alloca(16), 1, 3);
  char *p50 = alloca(16);
  void *p5 = fn2(1, p50, 3);

  for (int i = 0; i < 10; i++) {
    char *p6 = alloca(16);
  }

  //ASSERT(16, p1 - p2);
  //ASSERT(16, p2 - p3);
  //ASSERT(16, p3 - p4);

  memcpy(p1, "0123456789abcdef", 16);
  memcpy(p2, "ghijklmnopqrstuv", 16);
  memcpy(p3, "wxy", 3);

  ASSERT(0, memcmp(p1, "0123456789abcdef", 16));
  ASSERT(0, memcmp(p2, "ghijklmnopqrstuv", 16));
  ASSERT(0, memcmp(p3, "wxy", 3));

  ASSERT(1, p50 == p5);

  printf("OK\n");
  return 0;
}
