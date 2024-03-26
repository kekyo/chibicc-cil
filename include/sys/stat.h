#ifndef __SYS_STAT_H
#define __SYS_STAT_H

#include <time.h>
#include <sys/types.h>

struct timespec {
  time_t tv_sec;
  long tv_nsec;
};

struct stat {
  off_t st_size;
  struct timespec st_atim;
  struct timespec st_mtim;
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
};

int stat(char *pathname, struct stat *statbuf);

#endif
