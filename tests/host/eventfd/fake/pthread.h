/* Host test shim: counts how thread switching is held off.  */
#define PTHREAD_UNSAFE
extern int __pthread_disable_ints (void);
extern int __pthread_enable_ints (void);
extern void pthread_yield (void);
