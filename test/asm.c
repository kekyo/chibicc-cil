#include "test.h"

char *asm_fn1(void) {
  __asm__("ldc.i4.s 50\n\t"
      "conv.i\n\t"
      "ret");
}

char *asm_fn2(void) {
  __asm__ inline volatile("ldc.i4.s 55\n\t"
                      "conv.i\n\t"
                      "ret");
}

int getptrsize() __asm__("asm_foobar");
extern int ext1 __asm__("asm_baz");

int asm_foobar() {
  return 1;
}

int asm_baz = 123;

int main() {
  ASSERT(50, asm_fn1());
  ASSERT(55, asm_fn2());

  ASSERT(1, getptrsize());
  ASSERT(123, ext1);

  printf("OK\n");
  return 0;
}
