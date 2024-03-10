#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

typedef __builtin_nuint size_t;
typedef __builtin_nint ptrdiff_t;
typedef unsigned int wchar_t;
typedef long max_align_t;

#endif
