/* Internal UnixLib sys/stat.h
 * Copyright (c) 2008-2013 UnixLib Developers
 */

#ifndef __SYS_STAT_H
#include_next <sys/stat.h>
#endif

#if !defined(__INTERNAL_SYS_STAT_H) && defined(__SYS_STAT_H)
#define	__INTERNAL_SYS_STAT_H

__BEGIN_DECLS

int __stat (unsigned __objtype, unsigned __loadaddr, unsigned __execaddr,
	    unsigned __length, unsigned __attr, struct stat *buf);

/* 2026: copy a struct stat into a struct stat64.  RISC OS sizes are
   unsigned 32-bit (files up to 4GB-1), kept as they are in the 32-bit
   st_size; read them back as unsigned.  */
static inline void
__stat_to_stat64 (const struct stat *__st, struct stat64 *__st64)
{
  __st64->st_dev = __st->st_dev;
  __st64->st_ino = __st->st_ino;
  __st64->st_mode = __st->st_mode;
  __st64->st_nlink = __st->st_nlink;
  __st64->st_uid = __st->st_uid;
  __st64->st_gid = __st->st_gid;
  __st64->st_rdev = __st->st_rdev;
  __st64->st_size = (unsigned long) __st->st_size;
#if defined __USE_MISC || defined __USE_XOPEN2K8
  __st64->st_atim = __st->st_atim;
  __st64->st_mtim = __st->st_mtim;
  __st64->st_ctim = __st->st_ctim;
#else
  __st64->st_atime = __st->st_atime;
  __st64->st_atimensec = __st->st_atimensec;
  __st64->st_mtime = __st->st_mtime;
  __st64->st_mtimensec = __st->st_mtimensec;
  __st64->st_ctime = __st->st_ctime;
  __st64->st_ctimensec = __st->st_ctimensec;
#endif
  __st64->st_blksize = __st->st_blksize;
  __st64->st_blocks = __st->st_blocks;
}

__END_DECLS

#endif
