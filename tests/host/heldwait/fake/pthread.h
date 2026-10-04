/* Host test shim.  */
extern int __pthread_disable_ints (void);
extern int __pthread_enable_ints (void);
extern void pthread_yield (void);
extern void pthread_testcancel (void);
extern int __pthread_held_wait (int);
