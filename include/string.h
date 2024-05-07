#ifndef __STRING_H
#define __STRING_H

#include <stddef.h>

int strcmp(char *s1, char *s2);
int memcmp(void *s1, void *s2, size_t n);
size_t strlen(char *p);
int strncmp(char *s1, char *s2, size_t n);
void *memcpy(void *dst, void *src, size_t n);
char *strndup(char *p, size_t n);
char *strdup(char *p);
char *strtok(char *str, const char *delim);
char *strstr(char *haystack, char *needle);
char *strchr(char *s, int c);
double strtod(char *nptr, char **endptr);
unsigned long strtoul(char *nptr, char **endptr, int base);
char *strrchr(char *s, int c);
char *strncpy(char *dest, char *src, size_t n);
char *strncat(char *s1, char *s2, size_t n);

#endif
