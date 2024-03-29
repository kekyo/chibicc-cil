#ifndef __PTHREAD_H
#define __PTHREAD_H

typedef void *pthread_t;
typedef struct pthread_attr_t pthread_attr_t;

int pthread_create(pthread_t *newthread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t th, void **thread_return);

#endif
