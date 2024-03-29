#ifndef __ASSERT_H
#define __ASSERT_H

void __assert(const char *expr, const char *file, int line);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : __assert(#expr, __FILE__, __LINE__))
#endif

#endif
