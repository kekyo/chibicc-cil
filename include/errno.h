#ifndef __ERRNO_H
#define __ERRNO_H

int *__errno_location();
char *strerror(int errnum);

#define errno (*__errno_location())

#endif
