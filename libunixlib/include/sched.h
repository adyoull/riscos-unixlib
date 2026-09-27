/*
 * Written by Alex Waugh
 * Copyright (c) 2002-2010 UnixLib Developers
 */

#ifndef __SCHED_H
#define __SCHED_H

#ifndef __UNIXLIB_FEATURES_H
#include <features.h>
#endif

__BEGIN_DECLS

#include <bits/sched.h>

extern int sched_yield (void);

/* The range of priorities for POLICY.  UnixLib's scheduler ignores
   priorities, so both are 0 for every policy.  */
extern int sched_get_priority_min (int __policy) __THROW;
extern int sched_get_priority_max (int __policy) __THROW;

__END_DECLS

#endif /* __SCHED_H */
