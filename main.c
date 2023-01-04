#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
    return 1;
  }

  printf(".assembly Test\n");
  printf("{\n");
  printf("}\n");
  printf(".class public auto ansi abstract sealed beforefieldinit Test.Program\n");
  printf("  extends [mscorlib]System.Object\n");
  printf("{\n");
  printf("  .method public hidebysig static int32 Main(string[] args) cil managed\n");
  printf("  {\n");
  printf("    .entrypoint\n");
  printf("    ldc.i4 %d\n", atoi(argv[1]));
  printf("    ret\n");
  printf("  }\n");
  printf("}\n");
  return 0;
}
