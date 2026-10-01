/* Internal UnixLib unistd.h
 * Copyright (c) 2000-2008 UnixLib Developers
 */

#ifndef __UNISTD_H
#include_next <unistd.h>
#endif

#if !defined(__INTERNAL_UNISTD_H) && defined(__UNISTD_H)
#define __INTERNAL_UNISTD_H

__BEGIN_DECLS

/* Removes the suffix swap directory if it is empty.
   The filename passed to it is corrupted. */
extern void __unlinksuffix (char *__file);

extern void *__internal_sbrk (int __incr);
/* 2026: number of extra heap dynamic areas made (sys/brk.c).  */
extern int __heap_areas_made;
/* 2026: set while __internal_sbrk may start a new heap area.  */
extern int __heap_new_area_ok;
/* 2026: remove them at exit (called by __dynamic_area_exit).  */
extern void __dynamic_area_extra_exit (void);
/* 2026: will __internal_sbrk (INCR) start a new heap area?  */
extern int __heap_needs_new_area (int __incr);

__END_DECLS

#endif
