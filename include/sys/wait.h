#ifndef __SYS_WAIT_H
#define __SYS_WAIT_H

typedef int pid_t;

pid_t waitpid(pid_t pid, int *stat_loc, int options);

#endif
