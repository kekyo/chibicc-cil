#define ASSERT(x, y) assert(x, y, #y)

int getptrsize();
void assert(int expected, int actual, char *code);
void printf(char *arg);
