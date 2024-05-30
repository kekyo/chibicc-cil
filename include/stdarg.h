#ifndef __STDARG_H
#define __STDARG_H

typedef __builtin_va_list va_list;

extern void __builtin_va_start(va_list *ap, ...);
extern void *__builtin_va_arg(va_list *ap, ...);
extern void __builtin_va_copy(va_list *dest, va_list *src);

#define va_start(ap, arg) __builtin_va_start(&(ap), (arg))
#define va_arg(ap, tn) (*((tn *)__builtin_va_arg(&(ap), (tn *)0)))
#define va_end(ap)

#define va_copy(dest, src) __builtin_va_copy(&(dest), &(src))

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

typedef struct FILE FILE;

int vsprintf(char *buf, char *fmt, va_list ap);
int vfprintf(FILE *fp, char *fmt, va_list ap);

#endif
