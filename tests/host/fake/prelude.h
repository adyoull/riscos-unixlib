/* Host test prelude: rename the few calls the fake RISC OS provides
   (system headers first, so their declarations keep the real names). */
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
int fake_yield (void);
long fake_clock (void);
char *fake_getenv (const char *);
#define pthread_yield fake_yield
#define clock fake_clock
#define getenv fake_getenv
