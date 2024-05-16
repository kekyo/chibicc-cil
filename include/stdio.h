#ifndef __STDIO_H
#define __STDIO_H

#include <stddef.h>

typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(char *pathname, char *mode);
FILE *open_memstream(char **ptr, size_t *sizeloc);
long fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(void *ptr, size_t size, size_t nmemb, FILE *fp);
int fflush(FILE *fp);
int fclose(FILE *fp);
int ferror(FILE *fp);
int fputc(int c, FILE *fp);
int feof(FILE *fp);
int printf(char *fmt, ...);
int fprintf(FILE *fp, char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);

#endif
