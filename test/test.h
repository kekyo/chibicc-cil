extern void __builtin_va_start(__builtin_va_list *ap, ...);
extern void *__builtin_va_arg(__builtin_va_list *ap, ...);

#define va_list __builtin_va_list
#define va_start(ap, arg) __builtin_va_start(&(ap), (arg))
#define va_arg(ap, tn) (*((tn *)__builtin_va_arg(&(ap), (tn *)0)))
#define va_end(ap)

#define ASSERT(x, y) assert(x, y, #y)

int getptrsize();
void assert(int expected, int actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);
