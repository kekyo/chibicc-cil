typedef __builtin_nint intptr_t;
typedef __builtin_nuint uintptr_t;
typedef __builtin_nuint size_t;
typedef __builtin_va_list va_list;

extern void __builtin_trap();

extern void __builtin_va_start(va_list *ap, ...);
extern void *__builtin_va_arg(va_list *ap, ...);

#define va_start(ap, arg) __builtin_va_start(&(ap), (arg))
#define va_arg(ap, tn) (*((tn *)__builtin_va_arg(&(ap), (tn *)0)))
#define va_end(ap)

#define ASSERT(x, y) assert(x, y, #y)

void assert(long expected, long actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, size_t n);
void exit(int code);
int getptrsize();
