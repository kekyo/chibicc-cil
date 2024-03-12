#ifndef __ASSERT_H
#define __ASSERT_H

void assert(long expected, long actual, char *code);

#define ASSERT(x, y) assert(x, y, #y)

#endif
