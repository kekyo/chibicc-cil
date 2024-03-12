#ifndef __STDLIB_H
#define __STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *buf, size_t size);
void free(void *p);

void exit(int code);

int mkstemp(char *template);

int atexit(void (*)(void));
char *getenv(const char *name);

#endif
