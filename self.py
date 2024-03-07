#!/usr/bin/python3
import re
import sys

print("""
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef _Bool bool;
typedef __builtin_nuint size_t;
typedef __builtin_va_list va_list;

typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

extern void __builtin_trap();

extern void __builtin_va_start(va_list *ap, ...);
extern void *__builtin_va_arg(va_list *ap, ...);

struct stat {
  char _[512];
};

typedef struct {
  size_t gl_pathc;
  char **gl_pathv;
  size_t gl_offs;
  char _[512];
} glob_t;

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *buf, size_t size);
void free(void *p);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
FILE *open_memstream(char **ptr, size_t *sizeloc);
long fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(void *ptr, size_t size, size_t nmemb, FILE *fp);
int fflush(FILE *fp);
int fclose(FILE *fp);
int fputc(int c, FILE *fp);
int feof(FILE *fp);
static void assert() {}
int glob(char *pattern, int flags, void *errfn, glob_t *pglob);
void globfree(glob_t *pglob);
char *dirname(char *path);
int strcmp(char *s1, char *s2);
int strncasecmp(char *s1, char *s2, size_t n);
int memcmp(void *s1, void *s2, size_t n);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int fprintf(FILE *fp, char *fmt, ...);
int vfprintf(FILE *fp, char *fmt, va_list ap);
size_t strlen(char *p);
int strncmp(char *s1, char *s2, size_t n);
void *memcpy(void *dst, void *src, size_t n);
char *strndup(char *p, size_t n);
char *strdup(char *p);
int isspace(int c);
int ispunct(int c);
int isalpha(int c);
int isdigit(int c);
int isxdigit(int c);
char *strstr(char *haystack, char *needle);
char *strchr(char *s, int c);
double strtod(char *nptr, char **endptr);
static void va_end(va_list ap) {}
unsigned long strtoul(char *nptr, char **endptr, int base);
void exit(int code);
char *basename(char *path);
char *strrchr(char *s, int c);
int unlink(char *pathname);
int mkstemp(char *template);
int close(int fd);
typedef int pid_t;
int posix_spawnp(pid_t *pid,
  const char *path,
  const void *file_actions,
  const void *attrp,
  char *const argv[],
  char *const envp[]);
pid_t waitpid(pid_t pid, int *stat_loc, int options);
int atexit(void (*)(void));
char *getenv(const char *name);
static int WIFEXITED(int status) { return status & 0x80; }
static int WIFSIGNALED(int status)	{ return 0; }
FILE *open_memstream(char **ptr, size_t *sizeloc);
char *strncpy(char *dest, char *src, size_t n);
int stat(char *pathname, struct stat *statbuf);
char *strncat(char *s1, char *s2, size_t n);
""")

for path in sys.argv[1:]:
    with open(path) as file:
        s = file.read()
        s = re.sub(r'\\\n', '', s)
        s = re.sub(r'^\s*#.*', '', s, flags=re.MULTILINE)
        s = re.sub(r'"\n\s*"', '', s)
        s = re.sub(r'\berrno\b', '*__errno_location()', s)
        s = re.sub(r'\btrue\b', '1', s)
        s = re.sub(r'\bfalse\b', '0', s)
        s = re.sub(r'\bNULL\b', '0', s)
        s = re.sub(r'\bva_start\(([^)]*),([^)]*)\)', '__builtin_va_start(&(\\1), (\\2))', s)
        s = re.sub(r'\bva_arg\(([^)]*),([^)]*)\)', '(*((\\2 *)__builtin_va_arg(&(\\1), (\\2 *)0)))', s)
        s = re.sub(r'\bunreachable\(\)', 'error("unreachable")', s)
        s = re.sub(r'\bMIN\(([^)]*),([^)]*)\)', '((\\1)<(\\2)?(\\1):(\\2))', s)
        print(s)
