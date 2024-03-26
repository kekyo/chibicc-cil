#include "test.h"

char *asm_fn1(void) {
  asm("ldc.i4.s 50\n\t"
      "conv.i\n\t"
      "ret");
}

char *asm_fn2(void) {
  asm inline volatile("ldc.i4.s 55\n\t"
                      "conv.i\n\t"
                      "ret");
}

int main() {
  ASSERT(50, asm_fn1());
  ASSERT(55, asm_fn2());

  printf("OK\n");
  return 0;
}
