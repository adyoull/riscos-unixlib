#include "../kernel.h"
int __ul_seterr (const _kernel_oserror *e, int en);
void __pthread_enable_ints (void);
void __pthread_disable_ints (void);
static inline int __set_errno_f (int e) { errno = e; return -1; }
#define __set_errno(e) __set_errno_f (e)
#define EOPSYS 200
