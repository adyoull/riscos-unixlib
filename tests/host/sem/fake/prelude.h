/* Host test shim, included first: the host's own pthread types get other
   names, so fake/pthread.h can define UnixLib's.  */
#define pthread_t host_pthread_t
#define pthread_cond_t host_pthread_cond_t
#define pthread_rwlock_t host_pthread_rwlock_t
#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#undef pthread_t
#undef pthread_cond_t
#undef pthread_rwlock_t
