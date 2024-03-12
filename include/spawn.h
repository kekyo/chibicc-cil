#ifndef SPAWN_H
#define SPAWN_H

#include <sys/wait.h>

static int WIFEXITED(int status) { return status & 0x80; }
static int WIFSIGNALED(int status)	{ return 0; }

int posix_spawnp(pid_t *pid,
  const char *path,
  const void *file_actions,
  const void *attrp,
  char *const argv[],
  char *const envp[]);

#endif
