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
   however much memory was free.  Physical memory pool areas are capped
   the same way.

   But RISC OS 5 does put a new area where it is asked to (R3), and two
   areas side by side work as one block (tests/riscos/daprobe.c, Pi 4).
   So when the heap reaches the end of its area, another area is created
   directly after it, named after the first ("OpenTTD Heap 2"...), and
   the heap simply carries on: to malloc it is one contiguous heap, and
   a single block can be bigger than 128 MB.

   If an area can't be put directly after (the address is taken), the
   heap carries on in an area wherever RISC OS puts it: a new "segment".
   malloc copes with that gap as with a foreign sbrk (fenceposts around
   the old top), but a block can't cross it.  Only the first sbrk of a
   malloc request may start a segment (__heap_new_area_ok).

   All the areas are removed at exit with the first (see
   __dynamic_area_exit in _syslib.s).  RLIMIT_DATA still gives the first
   area's maximum.  */
#define HEAP_AREAS_MAX 64
#define HEAP_AREA_MIN_MAX (128u << 20)

struct heap_area
{
  int num;
  unsigned int base, max;
};
static struct heap_area heap_areas[HEAP_AREAS_MAX];
static int heap_area_count;	/* 0 until the heap first needs a list */
static int seg_start;		/* first area of the current segment */
static int grow_idx;		/* the area holding __ul_memory.dalimit */
static int chain_failed;	/* brk_da couldn't add an area after */
static unsigned int heap_max_ok;	/* a maximum RISC OS accepted at a base
					   it chose (2026 audit) */
static unsigned int chain_blocked;	/* a base where an area couldn't be
					   made (0: none): don't keep trying */
static char heap_area_name[48];

/* Number of segments started so far (malloc handles that call as
   non-contiguous).  */
int __heap_areas_made;
/* Set by malloc around the one sbrk call that may start a new segment
   (not its follow-up calls, which must extend the space the first got).  */
int __heap_new_area_ok;

/* Start the list with the area UnixLib made at start-up.  */
static int
heap_list_init (void)
{
  unsigned int base, max;

  if (heap_area_count)
    return 0;
  if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(3) | _OUT(5), 2,
	     __ul_global.dynamic_num, &base, &max) != NULL)
    return -1;
  heap_areas[0].num = __ul_global.dynamic_num;
  heap_areas[0].base = base;
  heap_areas[0].max = max;
  heap_area_count = 1;
  seg_start = grow_idx = 0;
  return 0;
}

/* Create the heap's next area, at BASE (or where RISC OS likes, -1).
   Returns 0 and appends it to the list, or -1.  */
static int
heap_make_area (int base)
{
  const char *name = NULL;
  unsigned int first_max = 0, want, got_base, max, i, n;
  int num;

  if (heap_area_count >= HEAP_AREAS_MAX)
    return -1;
  if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(5) | _OUT(8), 2,
	     heap_areas[0].num, &first_max, &name) != NULL)
    first_max = 0, name = NULL;
  for (i = 0; name && name[i] >= ' ' && i < sizeof (heap_area_name) - 5; i++)
    heap_area_name[i] = name[i];
  heap_area_name[i++] = ' ';
  n = heap_area_count + 1;
  if (n >= 10)
    heap_area_name[i++] = '0' + n / 10;
  heap_area_name[i++] = '0' + n % 10;
  heap_area_name[i] = '\0';

  /* Ask for the first area's maximum (at least 128 MB); if RISC OS
     refuses (older systems with less address space), ask for less, down
     to 1 MB.  At a given base, once a maximum has worked before, a
     refusal means the space is taken: don't go on halving.  */
  want = first_max > HEAP_AREA_MIN_MAX ? first_max : HEAP_AREA_MIN_MAX;
  if (heap_max_ok && want > heap_max_ok)
    want = heap_max_ok;
  for (;;)
    {
      max = 0;
      if (_swix (OS_DynamicArea, _INR(0,8) | _OUT(1) | _OUT(3) | _OUT(5),
		 0, -1, 0, base, 0x80, want, 0, 0, heap_area_name,
		 &num, &got_base, &max) == NULL)
	break;
      if (base != -1 && heap_max_ok && want <= heap_max_ok)
	return -1;
      want /= 2;
      if (want < (1u << 20))
	return -1;
    }
  /* 2026 (audit): only a maximum accepted where RISC OS chose the base
     says how big an area may be.  At a fixed base it can be small only
     because a gap is small: learning that made every later area that
     size, so one 1 MB gap after the heap stopped it at about first + 63
     MB.  */
  if (base == -1 && heap_max_ok < want)
    heap_max_ok = want;
  if ((base != -1 && got_base != (unsigned int) base) || max == 0)
    {
      _swix (OS_DynamicArea, _INR(0,1), 1, num);
      return -1;
    }
  heap_areas[heap_area_count].num = num;
  heap_areas[heap_area_count].base = got_base;
  heap_areas[heap_area_count].max = max;
  heap_area_count++;
  return 0;
}

/* Add an area directly after the last one of the current segment.  */
static int
heap_chain_area (void)
{
  const struct heap_area *last = &heap_areas[heap_area_count - 1];
  unsigned int next = last->base + last->max;

  /* Not past the top of the address space, and not where it failed
     before in this segment.  */
  if (next <= last->base || next == chain_blocked)
    return -1;
  if (heap_make_area ((int) next) != 0)
    {
      chain_blocked = next;
      return -1;
    }
  return 0;
}

/* Remove areas at the end of the current segment that hold nothing (made
   ahead of need, or for a growth that then failed).  */
static void
heap_drop_empty (void)
{
  while (heap_area_count - 1 > grow_idx
	 || (heap_area_count - 1 > seg_start
	     && heap_areas[heap_area_count - 1].base >= __ul_memory.dalimit))
    {
      _swix (OS_DynamicArea, _INR(0,1), 1,
	     heap_areas[heap_area_count - 1].num);
      heap_area_count--;
    }
  if (grow_idx >= heap_area_count)
    grow_idx = heap_area_count - 1;
  __ul_global.dynamic_num = heap_areas[grow_idx].num;
}

/* Room left in the current segment's areas, from dalimit on.  */
static unsigned int
heap_room (void)
{
  unsigned int room = 0;
  int i;
  for (i = grow_idx; i < heap_area_count; i++)
    room += heap_areas[i].base + heap_areas[i].max
	    - (i == grow_idx ? __ul_memory.dalimit : heap_areas[i].base);
  return room;
}

/* Called by __dynamic_area_exit: remove the heap's other areas (the
   current one, __ul_global.dynamic_num, is removed by the caller).  */
void
__dynamic_area_extra_exit (void)
{
  int i;

  for (i = 0; i < heap_area_count; i++)
    if (heap_areas[i].num != __ul_global.dynamic_num)
      _swix (OS_DynamicArea, _INR(0,1), 1, heap_areas[i].num);
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

  /* If the new address exceeds our current allocation from the
     dynamic area, then we must attempt to claim more memory for
     the dynamic area.  */
  if (addr > mem->dalimit)
    {
      /* Calculate the amount we want to increase the dynamic area
	 by.  Align that amount up to a multiple of 32K to reduce number
	 of expensive sbrk calls.

	 This is done because OS_ChangeDynamicArea can be expensive,
	 so smaller [s]brk increments will fit inside 'dalimit'.  */
      unsigned int want = ((addr - mem->dalimit) + __DA_WIMPSLOT_ALIGNMENT)
			  & ~__DA_WIMPSLOT_ALIGNMENT;

      /* 2026: grow area by area: fill the area holding dalimit to its
	 maximum, then go on in the next one (made directly after it if
	 there isn't one yet).  */
      chain_failed = 0;
      if (heap_list_init () != 0)
	return __set_errno (ENOMEM);
      /* Make every area the growth needs first, so that if one can't be
	 made (and malloc goes on in a new segment) no memory has been
	 committed here for nothing.  */
      while (heap_room () < want)
	if (heap_chain_area () != 0)
	  {
	    chain_failed = 1;
	    heap_drop_empty ();
	    return __set_errno (ENOMEM);
	  }
      while (want > 0)
	{
	  const struct heap_area *a = &heap_areas[grow_idx];
	  unsigned int room = a->base + a->max - mem->dalimit, step;

	  if (room == 0)
	    {
	      grow_idx++;
	      gbl->dynamic_num = heap_areas[grow_idx].num;
	      continue;
	    }
	  step = want < room ? want : room;
	  regs[0] = a->num;
	  regs[1] = (int) step;
	  if (__os_swi (OS_ChangeDynamicArea, regs))
	    {
	      unsigned int size;
#ifdef DEBUG
	      debug_printf ("-- brk: OS_ChangeDynamicArea failed\n");
#endif
	      /* Failed to allocate the memory, so return an error.  What
		 was added before stays.  The area may have grown part of
		 the way: take its size from RISC OS.  */
	      if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(2), 2, a->num,
			 &size) == NULL
		  && a->base + size > mem->dalimit
		  && size <= a->max)
		mem->dalimit = a->base + size;
	      heap_drop_empty ();
	      return __set_errno (ENOMEM);
	    }

	  /* Record the new maximum address space: what RISC OS says it
	     added.  */
	  if ((unsigned int) regs[1] < step)
	    step = (unsigned int) regs[1];
	  mem->dalimit += step;
	  want = step < want ? want - step : 0;
	  if (step == 0)
	    {
	      heap_drop_empty ();
	      return __set_errno (ENOMEM);
	    }
	}
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
	 reducing the size of our dynamic area.  2026: only the area
	 holding dalimit (the others of the heap stay full).  */
      regs[0] = gbl->dynamic_num;
      if (heap_area_count
	  && addr < heap_areas[grow_idx].base)
	regs[1] = mem->dalimit - heap_areas[grow_idx].base;
      else
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

/* 2026: start a new segment of the heap: an area wherever RISC OS puts
   it, used when one can't be added directly after the last.  */
static int
heap_new_segment (void)
{
  struct ul_memory *mem = &__ul_memory;

  if (heap_make_area (-1) != 0)
    return -1;
  chain_blocked = 0;
  seg_start = grow_idx = heap_area_count - 1;
  __ul_global.dynamic_num = heap_areas[grow_idx].num;
  mem->dalomem = mem->dabreak = mem->dalimit = heap_areas[grow_idx].base;
  __heap_areas_made++;
  return 0;
}

/* 2026: for malloc: will __internal_sbrk (INCR) have to start a new
   segment (so the space can't be merged with the old top)?  Adds the
   areas needed directly after the current one if it can.  */
int
__heap_needs_new_area (int incr)
{
  const struct ul_memory *mem = &__ul_memory;
  unsigned int addr, need;

  if (__ul_global.dynamic_num == -1 || incr <= 0
      || mem->dabreak + (unsigned int) incr < mem->dabreak)
    return 0;
  addr = align (mem->dabreak + (unsigned int) incr);
  /* Fits in what the heap already has: no (and no SWI).  */
  if (addr <= mem->dalimit || heap_list_init () != 0)
    return 0;
  need = ((addr - mem->dalimit) + __DA_WIMPSLOT_ALIGNMENT)
	 & ~__DA_WIMPSLOT_ALIGNMENT;
  while (heap_room () < need)
    if (heap_chain_area () != 0)
      {
	heap_drop_empty ();
	return 1;
      }
  return 0;
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

      /* 2026: not past the top of the address space.  */
      if (oldbrk + (unsigned int) incr < oldbrk)
	return (void *) -1;
      if (incr != 0 && brk_da (oldbrk + incr) < 0)
	{
	  /* 2026: if no area could be added directly after the heap,
	     carry on in a new segment (only on the first sbrk of a malloc
	     request).  */
	  struct ul_memory saved = *mem;
	  int saved_num = gbl->dynamic_num, saved_count = heap_area_count;
	  int saved_seg = seg_start, saved_grow = grow_idx;

	  if (!__heap_new_area_ok || !chain_failed
	      || heap_new_segment () != 0)
	    return (void *) -1;
	  oldbrk = mem->dabreak;
	  if (brk_da (oldbrk + incr) < 0)
	    {
	      /* No memory for it after all: remove the new areas and go on
		 with the old segment.  */
	      while (heap_area_count > saved_count)
		{
		  heap_area_count--;
		  _swix (OS_DynamicArea, _INR(0,1), 1,
			 heap_areas[heap_area_count].num);
		}
	      __heap_areas_made--;
	      seg_start = saved_seg;
	      grow_idx = saved_grow;
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
