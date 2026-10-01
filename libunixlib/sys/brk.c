/* Change data segment size.
   Copyright (c) 2002, 2003, 2004, 2005, 2007, 2008 UnixLib Developers.  */

/****************************************************************************
 *
 * Memory is laid out in one of two ways, depending whether we are using
 * a RISC OS 3.5+ dynamic area for the heap. If a dynamic area is being
 * used then __ul_global.dynamic_num != -1.
 *
 * Case 1: No dynamic area
 *
 *    +-------+-------------+         +--------+.....+
 *    |       | heap ->     | ....... |        |     | ......
 *    +-------+-------------+         +--------+.....+
 *    ^       ^             ^->     <-^        ^     ^->
 * robase  rwlomem     rwbreak      stack      |  appspace_limit
 *                     stack_limit       appspace_himem
 *
 *
 * Case 2: Heap in dynamic area
 *                                                    /
 *    +-------+          +--------+.....+            /      +--------+.....+
 *    |       |  ....... |        |     | ......     \      | heap ->|     | ......
 *    +-------+          +--------+.....+            /      +--------+.....+
 *    ^       ^        <-^        ^     ^->         /       ^        ^     ^->
 * robase  rwlomem     stack      |  appspace_limit    dalomem       |  dalimit
 *         rwbreak          appspace_himem                        dabreak
 *         stack_limit
 *
 *
 * The stack initially decends (in chunks) downto __ul_memory.stack_limit, then
 * increases (in chunks) by increasing the wimpslot. If the malloc heap is
 * also in the wimpslot then it can also cause the wimpslot to extend.
 *
 * This file should be compiled without stack checking, as it is could
 * confuse malloc if the stack extension caused 'appspace_himem' to move
 * whilst malloc is trying to sbrk a region.
 *
 ***************************************************************************/

/* sys/brk.c: Complete rewrite by Peter Burwood, June 1997  */


#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>
#include <swis.h>
#include <sys/types.h>

#include <internal/os.h>
#include <internal/unix.h>
#include <pthread.h>

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

#define align(x) ((x + 3) & ~3)

/* 2026: a heap of several dynamic areas.

   RISC OS 5 gives a dynamic area created with OS_DynamicArea 0 a maximum
   of 128 MB, whatever maximum is asked for (measured on a Pi 4 by the
   OpenTTD port, 2026-10-01), so a heap in one area stopped at 128 MB
   however much memory was free.  When malloc's sbrk (__internal_sbrk)
   finds the current area full, another area is created, named after the
   first ("OpenTTD Heap 2", "OpenTTD Heap 3"...), and the heap carries on
   there.  malloc copes with the gap: it is the same as a foreign sbrk
   (fenceposts around the old top).  brk() and sbrk() work on the newest
   area.  All the areas are removed at exit with the first (see
   __dynamic_area_exit in _syslib.s).

   One allocation still has to fit in one area.  An area is first asked
   for with the first area's maximum (at least 128 MB), and if RISC OS
   refuses that or gives less than the request needs, once more with a
   maximum of just the request.

   After a switch, sbrk(0) is in the new area, brk() to an address in an
   old area fails, and RLIMIT_DATA still gives the first area's maximum.
   Memory in old areas is reused by malloc but only returned at exit.  */
#define HEAP_AREAS_MAX 64
#define HEAP_AREA_MIN_MAX (128u << 20)
static int heap_areas[HEAP_AREAS_MAX];	/* [0] is the first area */
static int heap_area_count;		/* 0 until a second area is made */
static char heap_area_name[48];

/* Number of areas made so far (malloc retries once after a new one).  */
int __heap_areas_made;
/* Set by malloc around the one sbrk call that may start a new area (not
   its follow-up calls, which must extend the space the first one got).  */
int __heap_new_area_ok;

/* Create area number N (2...) of the heap with room for NEED bytes and make
   it the current one.  Returns 0 on success.  */
static int
heap_new_area (unsigned int need)
{
  struct ul_memory *mem = &__ul_memory;
  struct ul_global *gbl = &__ul_global;
  const char *name = NULL;
  unsigned int first_max = 0, want, base, max, i, n;
  int num, tries;

  if (heap_area_count == 0)
    {
      heap_areas[0] = gbl->dynamic_num;
      heap_area_count = 1;
    }
  if (heap_area_count >= HEAP_AREAS_MAX)
    return -1;

  /* The first area's name and maximum.  */
  if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(5) | _OUT(8), 2,
	     heap_areas[0], &first_max, &name) != NULL)
    return -1;
  for (i = 0; name && name[i] >= ' ' && i < sizeof (heap_area_name) - 5; i++)
    heap_area_name[i] = name[i];
  heap_area_name[i++] = ' ';
  n = heap_area_count + 1;
  if (n >= 10)
    heap_area_name[i++] = '0' + n / 10;
  heap_area_name[i++] = '0' + n % 10;
  heap_area_name[i] = '\0';

  want = first_max > HEAP_AREA_MIN_MAX ? first_max : HEAP_AREA_MIN_MAX;
  need = (need + 0xFFFFu) & ~0xFFFFu;
  for (tries = 0; tries < 2; tries++)
    {
      if (want < need)
	want = need;
      /* R5 out: the maximum RISC OS actually gave.  */
      max = 0;
      if (_swix (OS_DynamicArea, _INR(0,8) | _OUT(1) | _OUT(3) | _OUT(5),
		 0, -1, 0, -1, 0x80, want, 0, 0, heap_area_name,
		 &num, &base, &max) == NULL)
	{
	  if (max >= need)
	    break;
	  /* Not enough: remove it.  */
	  _swix (OS_DynamicArea, _INR(0,1), 1, num);
	}
      /* Refused or too small: ask once more for just the request.  */
      if (want == need)
	return -1;
      want = need;
    }
  if (tries == 2)
    return -1;

  heap_areas[heap_area_count++] = num;
  __heap_areas_made++;
  gbl->dynamic_num = num;
  mem->dalomem = mem->dabreak = mem->dalimit = base;
  return 0;
}

/* Called by __dynamic_area_exit: remove the heap's other areas (the
   current one, __ul_global.dynamic_num, is removed by the caller).  */
void
__dynamic_area_extra_exit (void)
{
  int i;

  for (i = 0; i < heap_area_count; i++)
    if (heap_areas[i] != __ul_global.dynamic_num)
      _swix (OS_DynamicArea, _INR(0,1), 1, heap_areas[i]);
  heap_area_count = 0;
}

/* brk function for dynamic areas.  */
static int
brk_da (unsigned int addr)
{
  struct ul_memory *mem = &__ul_memory;
  struct ul_global *gbl = &__ul_global;
  int regs[10];

  /* Ensure requested address is aligned to a 4 byte boundry.  */
  addr = align (addr);

#ifdef DEBUG
  debug_printf ("-- brk_da: addr=%08x dalomem=%08x dabreak=%08x dalimit=%08x\n",
		addr, mem->dalomem, mem->dabreak, mem->dalimit);
#endif

  /* Check new limit isn't below minimum brk limit, i.e., dalomem
     Return EINVAL, because it doesn't make sense to return ENOMEM.  */
  if (addr < mem->dalomem)
    {
#ifdef DEBUG
      /* It would be interesting to know if this ever happens, so
	 for the time being it is marked with a flag to draw special
	 attention.  */
      debug_printf ("-- brk_da: addr (%08x) < dalomem (%08x)  !!! flag !!!\n",
		    addr, mem->dalomem);
#endif
      return __set_errno (EINVAL);
    }

  regs[0] = gbl->dynamic_num;

  /* If the new address exceeds our current allocation from the
     dynamic area, then we must attempt to claim more memory for
     the dynamic area.  */
  if (addr > mem->dalimit)
    {
      /* Calculate the amount we want to increase the dynamic area
	 by.  */
      regs[1] = addr - mem->dalimit;

      /* Align that amount up to a multiple of 32K to reduce number
	 of expensive sbrk calls.

	 This is done because OS_ChangeDynamicArea can be expensive,
	 so smaller [s]brk increments will fit inside 'dalimit'.  */
      regs[1] = ((regs[1] + __DA_WIMPSLOT_ALIGNMENT)
		 & ~__DA_WIMPSLOT_ALIGNMENT);
      if (__os_swi (OS_ChangeDynamicArea, regs))
	{
#ifdef DEBUG
	  debug_printf ("-- brk: OS_ChangeDynamicArea failed\n");
#endif
	  /* Failed to allocate the memory, so return an error.  */
	  return __set_errno (ENOMEM);
	}

      /* Record the new maximum address space.  */
      mem->dalimit += regs[1];
    }

  /* At this point, we know that we can always satisfy a request to
     increase or decrease the address space.  */

  if (addr > mem->dabreak)
    {
      /* The user is claiming more memory and we have enough space in
	 our dynamic area to cope.  */
      mem->dabreak = addr;
    }
  else
    {
      /* The user is freeing memory.  */

      /* New alloc system can cope with userland calling sbrk aswell
	 as the library.  Thus, we should honour a request to reduce
	 the brk limit.  Align the new limit to a page boundary.  */
      mem->dabreak = addr;

      /* See if we can give some memory back to the system by
	 reducing the size of our dynamic area.  */
      regs[1] = mem->dalimit - addr;

      /* Align size down to multiple of 32K, thereby sticking to the
	 rule that we provide a small buffer space to reduce the number
	 of calls to OS_ChangeDynamicArea.  */
      regs[1] = regs[1] & ~__DA_WIMPSLOT_ALIGNMENT;

      /* If regs[1] is non-zero and positive, then we have found
	 memory to give back.  */
      if (regs[1] > 0)
	{
	  /* OS_ChangeDynamicArea takes a signed integer, with negative
	     values meaning that memory is to be released.  */
	  regs[1] = -regs[1];

	  /* Ignore any error from __os_swi, since it can happen with a
	     request to reduce the size of the area which is only partially
	     satisfied.  Either way, regs[1] should have the +ve amount of
	     memory returned to the system.  */
	  __os_swi (OS_ChangeDynamicArea, regs);
	  mem->dalimit -= regs[1];
	}
    }

  return 0;
}

/* brk function for standard read/write area.  */
static int
brk_rw (unsigned int addr, int internal_call)
{
  struct ul_memory *mem = &__ul_memory;

  /* Ensure requested address is aligned to a 4 byte boundry.  */
  addr = align (addr);

#ifdef DEBUG
  debug_printf ("-- brk_rw: addr=%08x rwlomem=%08x rwbreak=%08x stack_limit=%08x stack=%08x\n",
		addr, mem->rwlomem, mem->rwbreak,
		mem->stack_limit, mem->stack);
#endif

  /* Check new limit isn't below minimum brk limit, i.e., rwlomem.
     Return EINVAL, because it doesn't make sense to return ENOMEM.  */
  if (addr < mem->rwlomem)
    {
#ifdef DEBUG
      /* It would be interesting to know if this ever happens, so
	 for the time being it is marked with a flag to draw special
	 attention.  */
      debug_printf ("-- brk_rw: addr (%08x) < rwlomem (%08x)  !!! flag !!!\n",
		    addr, mem->rwlomem);
#endif
      return __set_errno (EINVAL);
    }

  /* Heap is not in a dynamic area and is therefore below the stack.
     Make sure we don't run into the stack */
  if (addr > mem->stack)
    {
#ifdef DEBUG
      debug_printf ("-- brk_rw: addr > __ul_memory.stack (!!!)\n");
#endif
      /* No space before stack, so try to increase wimpslot
	 If this is a userland call then increasing the wimpslot is
	 likely to give unexpected results so don't bother */
      if (! internal_call)
	return __set_errno (ENOMEM);

      if (!__stackalloc_incr_wimpslot (addr - mem->appspace_himem))
	return __set_errno (ENOMEM);
    }
  else
    {
      /* Adjust stack limit.*/
      mem->stack_limit = addr;
    }

  /* Adjust break limit.
     This allows +ve or -ve sbrk increments.  */
  mem->rwbreak = addr;

  return 0;
}

#if 0
static int
__internal_brk (void *addr, int internalcall)
{

  if ((unsigned int) addr > mem->rwbreak)
    {
      /* struct rlimit rlim; */
      /* Inline version of
	 if (getrlimit (RLIMIT_DATA, &rlim) >= 0)
	   if ((u_char *) addr - (u_char *) rwlomem > rlim.rlim_cur) */
      if ((unsigned int) addr - (unsigned int) mem->rwlomem
	  > __u->limit[RLIMIT_DATA].rlim_cur)
	{
#ifdef DEBUG
	  debug_printf ("-- brk: addr (%08x) - rwlomem (%08x) [%08x]"
			" > RLIMIT_DATA (%08x)\n",
			addr, mem->rwlomem,
			addr - mem->rwlomem,
			__u->limit[RLIMIT_DATA].rlim_cur);
#endif
	  /* Need to increase the resource limit.  */
	  return __set_errno (ENOMEM);
	}
    }

  return 0;
}
#endif

int
brk (void *addr)
{
  struct ul_global *gbl = &__ul_global;

  if (gbl->pthread_system_running)
    __pthread_protect_unsafe ();

  if (gbl->dynamic_num == -1)
    return brk_rw ((unsigned int) addr, 0);

  return brk_da ((unsigned int) addr);
}

/* External calls to sbrk can only increase rwbreak up
   to __ul_memory.stack, and cannot increase the wimpslot as the
   stack will be in the way. */
void *
sbrk (intptr_t delta)
{
  struct ul_memory *mem = &__ul_memory;
  struct ul_global *gbl = &__ul_global;

#ifdef DEBUG
  debug_printf ("-- sbrk: incr=%d\n", delta);
#endif

  if (gbl->pthread_system_running)
    __pthread_protect_unsafe ();

  if (gbl->dynamic_num == -1)
    {
      /* Non-dynamic area case.  */
      unsigned int oldbrk = mem->rwbreak;

      /* If the user has requested a change in the data segment size,
	 then try to satisfy it.  */
      if (delta != 0
	  && brk_rw (mem->rwbreak + (unsigned int) delta, 0) < 0)
	{
	  /* Request failed.  */
	  return (void *) -1;
	}

      /* sbrk returns a pointer to the start of the area.  */
      return (void *) ((oldbrk > mem->stack_limit) ? mem->stack_limit : oldbrk);
    }
  else
    {
      unsigned int oldbrk = mem->dabreak;

      /* Dynamic area case.  */
      if (delta != 0
	  && brk_da (oldbrk + (unsigned int) delta) < 0)
	{
	  /* Request failed.  */
	  return (void *) -1;
	}

      /* Request succeeded, return pointer to start of the area.  */
      return (void *) oldbrk;
    }
}

/* 2026: would growing the current area by INCR take it past its maximum
   (rather than failing for lack of free memory, which a new area wouldn't
   help)?  */
static int
heap_area_full (unsigned int incr)
{
  const struct ul_memory *mem = &__ul_memory;
  unsigned int max, grow, addr = align (mem->dabreak + incr);

  /* Fits in what the area already has: not full (and no SWI).  */
  if (addr <= mem->dalimit)
    return 0;
  if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(5), 2,
	     __ul_global.dynamic_num, &max) != NULL)
    return 0;
  /* brk_da grows the area in steps of __DA_WIMPSLOT_ALIGNMENT + 1.  */
  grow = ((addr - mem->dalimit) + __DA_WIMPSLOT_ALIGNMENT)
	 & ~__DA_WIMPSLOT_ALIGNMENT;
  return mem->dalimit - mem->dalomem + grow > max;
}

/* 2026: for malloc: will __internal_sbrk (INCR) have to start a new
   area?  */
int
__heap_needs_new_area (int incr)
{
  return __ul_global.dynamic_num != -1 && incr > 0
	 && heap_area_full ((unsigned int) incr);
}

/* sbrk for internal UnixLib callers (i.e. malloc) that are aware that
   space allocated may not be contiguous.  No need to be thread safe
   as the internal callers should already have ensured that context switching
   is disabled.  */
void *
__internal_sbrk (int incr)
{
  struct ul_memory *mem = &__ul_memory;
  struct ul_global *gbl = &__ul_global;

#ifdef DEBUG
  debug_printf ("-- __internal_sbrk: incr=%d\n", incr);
#endif

  if (incr < 0)
    return (void *) -1;

  if (gbl->dynamic_num == -1)
    {
      /* Dynamic areas are not in use.  */
      unsigned int oldbrk;
      /* Record once we have returned some memory above the stack,
	 to ensure all subsequent memory is also from above the stack,
	 otherwise malloc would get confused */
      static int overstack = 0;

      if (overstack || mem->rwbreak + incr >= mem->stack)
	{
	  oldbrk = mem->appspace_himem;
	  overstack = 1;
	}
      else
	oldbrk = mem->rwbreak;

      if (incr != 0 && brk_rw (oldbrk + incr, 1) < 0)
	return (void *) -1;

      return (void *) oldbrk;
    }
  else
    {
      /* Dynamic areas are in use.  */
      unsigned int oldbrk = mem->dabreak;

      if (incr != 0 && brk_da (oldbrk + incr) < 0)
	{
	  /* 2026: if the area is full, carry on in a new one.  */
	  struct ul_memory saved = *mem;
	  int saved_num = gbl->dynamic_num;

	  if (!__heap_new_area_ok || !heap_area_full (incr)
	      || heap_new_area (incr) != 0)
	    return (void *) -1;
	  oldbrk = mem->dabreak;
	  if (brk_da (oldbrk + incr) < 0)
	    {
	      /* No memory for it after all: remove the new area and go on
		 with the old one.  */
	      _swix (OS_DynamicArea, _INR(0,1), 1, gbl->dynamic_num);
	      heap_area_count--;
	      __heap_areas_made--;
	      gbl->dynamic_num = saved_num;
	      mem->dalomem = saved.dalomem;
	      mem->dabreak = saved.dabreak;
	      mem->dalimit = saved.dalimit;
	      return (void *) -1;
	    }
	}

      return (void *) oldbrk;
    }
}
