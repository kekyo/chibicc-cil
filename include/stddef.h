#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

#define __attribute__(x)
#define __nonnull(x)
#define __ASMNAME(cname) cname

typedef __builtin_uintptr size_t;
typedef __builtin_intptr ssize_t;
typedef __builtin_intptr ptrdiff_t;

typedef unsigned int wchar_t;
typedef unsigned int wint_t;
typedef long max_align_t;

extern __builtin_uintptr __builtin_sizeof_intptr;
extern __builtin_uintptr __builtin_sizeof_uintptr;
extern __builtin_intptr __builtin_intptr_max;
extern __builtin_intptr __builtin_intptr_min;
extern __builtin_uintptr __builtin_uintptr_max;

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

extern int __builtin_ffs(int x);
extern int __builtin_ffsl(long x);
extern int __builtin_ffsll(long long x) __asm__("__builtin_ffsl");

extern int __builtin_clz(int x);
extern int __builtin_clzl(long x);
extern int __builtin_clzll(long long x) __asm__("__builtin_clzl");

#endif
