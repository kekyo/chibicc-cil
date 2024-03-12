#ifndef __GLOB_H
#define __GLOB_H

#include <stddef.h>

typedef struct {
  size_t gl_pathc;
  char **gl_pathv;
  size_t gl_offs;
  char _[512];
} glob_t;

int glob(char *pattern, int flags, void *errfn, glob_t *pglob);
void globfree(glob_t *pglob);

#endif
