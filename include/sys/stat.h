#ifndef __SYS_STAT_H
#define __SYS_STAT_H

struct stat {
  char _[512];
};

int stat(char *pathname, struct stat *statbuf);

#endif
