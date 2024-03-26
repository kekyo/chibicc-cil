#include "test.h"
#include <stddef.h>

typedef struct {
  int a;
  char b;
  int c;
  double d;
  char *e;
  int f;
} T;

int main() {
  ASSERT(0, offsetof(T, a));
  ASSERT(4, offsetof(T, b));
  ASSERT(8, offsetof(T, c));
  ASSERT(16, offsetof(T, d));
  ASSERT(24, offsetof(T, e));
  ASSERT(24 + getptrsize(), offsetof(T, f));

  printf("OK\n");
  return 0;
}
