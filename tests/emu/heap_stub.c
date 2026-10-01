/* Entry points for tests/emu/heap_test.py: the real malloc (stdlib/alloc.c)
   and heap sbrk (sys/brk.c) from build/work/build/.libs/libunixlib.a, run in
   the Unicorn ARM emulator with OS_DynamicArea and OS_ChangeDynamicArea
   faked.  Not a RISC OS program.  */
#include <stddef.h>
#include <stdlib.h>
#include <internal/unix.h>

extern void __dynamic_area_extra_exit (void);
extern void __ul_malloc_init (void);

void
t_init (int area, unsigned int base)
{
  __ul_global.dynamic_num = area;
  __ul_global.pagesize = 4096;
  __ul_global.pthread_system_running = 0;
  __ul_memory.dalomem = __ul_memory.dabreak = __ul_memory.dalimit = base;
  __ul_malloc_init ();
}

void *t_malloc (size_t n) { return malloc (n); }
void t_free (void *p) { free (p); }
void *t_realloc (void *p, size_t n) { return realloc (p, n); }
void t_exit (void) { __dynamic_area_extra_exit (); }
int t_area (void) { return __ul_global.dynamic_num; }
