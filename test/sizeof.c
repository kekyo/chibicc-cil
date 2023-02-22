#include "test.h"

int main() {
  ASSERT(1, sizeof(char));
  ASSERT(2, sizeof(short));
  ASSERT(2, sizeof(short int));
  ASSERT(2, sizeof(int short));
  ASSERT(4, sizeof(int));
  ASSERT(8, sizeof(long));
  ASSERT(8, sizeof(long int));
  ASSERT(8, sizeof(long int));
  ASSERT(getptrsize(), sizeof(char *));
  ASSERT(getptrsize(), sizeof(int *));
  ASSERT(getptrsize(), sizeof(long *));
  ASSERT(getptrsize(), sizeof(int **));
  ASSERT(getptrsize(), sizeof(int(*)[4]));
  ASSERT(getptrsize() * 4, sizeof(int*[4]));
  ASSERT(16, sizeof(int[4]));
  ASSERT(48, sizeof(int[3][4]));
  ASSERT(8, sizeof(struct {int a; int b;}));

  ASSERT(8, sizeof(-10 + (long)5));
  ASSERT(8, sizeof(-10 - (long)5));
  ASSERT(8, sizeof(-10 * (long)5));
  ASSERT(8, sizeof(-10 / (long)5));
  ASSERT(8, sizeof((long)-10 + 5));
  ASSERT(8, sizeof((long)-10 - 5));
  ASSERT(8, sizeof((long)-10 * 5));
  ASSERT(8, sizeof((long)-10 / 5));

  ASSERT(1, ({ char i; sizeof(++i); }));
  ASSERT(1, ({ char i; sizeof(i++); }));

  ASSERT(getptrsize(), sizeof(int(*)[10]));
  ASSERT(getptrsize(), sizeof(int(*)[][10]));

  printf("OK\n");
  return 0;
}
