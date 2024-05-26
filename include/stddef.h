#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

typedef __builtin_uintptr size_t;
typedef __builtin_intptr ssize_t;
typedef __builtin_intptr ptrdiff_t;
typedef unsigned int wchar_t;
typedef long max_align_t;

extern __builtin_uintptr __builtin_sizeof_intptr;
extern __builtin_uintptr __builtin_sizeof_uintptr;
extern __builtin_intptr __builtin_intptr_max;
extern __builtin_intptr __builtin_intptr_min;
extern __builtin_uintptr __builtin_uintptr_max;

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
