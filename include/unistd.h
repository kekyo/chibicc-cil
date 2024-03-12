#ifndef __UNISTD_H
#define __UNISTD_H

void __builtin_trap();

int unlink(char *pathname);
int close(int fd);

#endif
