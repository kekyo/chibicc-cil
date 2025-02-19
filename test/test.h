typedef __builtin_intptr intptr_t;
typedef __builtin_uintptr uintptr_t;
typedef __builtin_uintptr size_t;
typedef __builtin_va_list va_list;

extern void __builtin_trap();

#define ASSERT(x, y) assert(x, y, #y)

void assert(long expected, long actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int vsprintf(char *buf, char *fmt, va_list ap);
int strcmp(char *p, char *q);
int strncmp(char *p, char *q, size_t n);
int memcmp(char *p, char *q, size_t n);
void exit(int code);
int getptrsize();
long strlen(char *s);
void *memcpy(void *dest, void *src, size_t n);
void *alloca(size_t size);
