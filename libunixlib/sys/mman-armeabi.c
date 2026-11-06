/* Map or unmap memory.
   Copyright (c) 1996-2021 UnixLib Developers.  */

/* Definitions for BSD-style memory management.  */
/* sys/mman.c: Written by Peter Burwood, 1 November 1996, June 1997  */
/* sys/mman-armeabi.c: Adapted from the above by Lee Noar, March 2021  */

/* 2026: this file's own calls of the __armeabi_fd_* helpers don't record
   a caller (see __UNIXLIB_FDOP_TRACE in internal/fd.h).  */
#define __UNIXLIB_FDOP_INTERNAL 1

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <swis.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <internal/os.h>
#include <internal/unix.h>
#include <internal/swiparams.h>
#include <internal/fd.h>
#include <internal/dev.h>
#include <internal/local.h>
#include <internal/symlinks.h>
#include <unixlib/local.h>
#include <sys/param.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>

/* 2026: in a diagnostic build, mmap and msync record where they were
   called from (internal/fd.h).  */
#if __UNIXLIB_FDOP_TRACE
extern const void *__armeabi_fd_caller;
#  define FDOP_CALLER() (__armeabi_fd_caller = __builtin_return_address (0))
#else
#  define FDOP_CALLER() ((void) 0)
#endif

#define page_align(gbl, len) \
  (((len) + (gbl)->pagesize - 1) & ~((gbl)->pagesize - 1))

extern void __mmap_page_copy (void * dst, void * src, int len);

/* 2026: file offsets are kept by ARMEABISupport as one 32-bit word and
   used here as unsigned, so mmap64 can map from 2GB up to 4GB-1 (the
   largest RISC OS file).  Positions are moved with lseek64, which also
   saves and restores a file position above 2GB.  */
extern __off64_t lseek64 (int __fd, __off64_t __offset, int __whence);
#define FILE_OFFSET(o) ((__off64_t) (unsigned long) (o))
void *__mmap_offset32 (void *addr, size_t len, int prot, int flags, int fd,
		       unsigned long offset);

static char coredump_dir[256];

void
__init_coredump (const char *dir)
{
  size_t dirlen = strlen (dir);

  /* Allow at least 16 bytes for the coredump filenames inside user supplied
     directory.  */
  if (dirlen < sizeof (coredump_dir)-16)
    strcpy (coredump_dir, dir);
}

const _kernel_oserror *
__unixlib_write_coredump (const char *dir)
{
  if (dir != NULL)
    __init_coredump (dir);

  size_t dirlen = strlen (coredump_dir);
  if (dirlen == 0)
    return NULL;

  /* Remove ending dot if there is one.  */
  if (coredump_dir[dirlen - 1] == '.')
    coredump_dir[--dirlen] = '\0';

  /* Create the coredump directory, with default number of entries.  */
  const _kernel_oserror *roerr;
  if ((roerr = SWI_OS_File_CreateDirectory (coredump_dir)) != NULL)
    return roerr;

  /* Write application space data.  */
  strcpy (coredump_dir + dirlen, ".app");
  const struct ul_memory *mem = &__ul_memory;
  roerr = SWI_OS_File_SaveBlockLoadExec (coredump_dir, 0x8000 /* load */,
					 mem->rwlomem /* exec */, 0x8000 /* start */,
					 mem->appspace_limit /* end */);

  /* Write main DA block (if there is one).  */
  if (__ul_global.dynamic_num != -1)
    {
      strcpy (coredump_dir + dirlen, ".da_main");
      if (roerr == NULL)
	roerr = SWI_OS_File_SaveBlockLoadExec (coredump_dir, mem->dalomem /* load */,
					       mem->dabreak /* exec */, mem->dalomem /* start */,
					       mem->dalimit /* end */);
    }

  /* Not really practical to dump all mmap allocations, there could be hundreds.  */
  coredump_dir[dirlen] = '\0';
  return roerr;
}


/* Free all mmapped memory.  This is called from __dynamic_area_exit.
   At that point, the thread system is not running.  */
void
__munmap_all (void)
{
  /* NOOP. This is handled by ARMEABISupport.  */
}

/* Map addresses starting near ADDR and extending for LEN bytes.  from
   OFFSET into the file FD describes according to PROT and FLAGS.  If ADDR
   is nonzero, it is the desired mapping address.  If the MAP_FIXED bit is
   set in FLAGS, the mapping will be at ADDR exactly (which must be
   page-aligned); otherwise the system chooses a convenient nearby address.
   The return value is the actual mapping address chosen or (void *) -1
   for errors (in which case `errno' is set).  A successful `mmap' call
   deallocates any previous mapping for the affected region.  */
void *
mmap (void * addr, size_t len, int prot, int flags, int fd, off_t offset)
{
  /* 2026: a negative offset is an error, as POSIX says (it used to seek
     nowhere and map whatever followed the file's current position).  */
  if (offset < 0)
    return (void *) __set_errno (EINVAL);
  FDOP_CALLER ();
  return __mmap_offset32 (addr, len, prot, flags, fd, (unsigned long) offset);
}

/* mmap, and mmap64 for offsets up to 4GB-1.  */
void *
__mmap_offset32 (void *addr, size_t len, int prot, int flags, int fd,
		 unsigned long offset)
{
  struct ul_global *gbl = &__ul_global;

  PTHREAD_UNSAFE

  /* We only support MAP_FIXED for an existing block.  */

  /* We don't support MAP_COPY.  */
  if (flags & MAP_COPY)
    return (void *) __set_errno (ENOSYS);

  /* We don't support MAP_PRIVATE. However, we will just use MAP_SHARED instead
     rather than changing all the user code calling us.  */

  void *result;
  _kernel_oserror *err;
  err = _swix (ARMEABISupport_MMapOp, _INR(0,6)|_OUT(0), ARMEABISUPPORT_MMAPOP_MAP,
							 addr,
							 len,
							 prot,
							 flags,
							 fd,
							 offset,
							 &result);
  if (err)
    return (void *) __set_errno (__errno_from_armeabisupport (err->errnum));
  if (!result)
    return (void *) __set_errno (ENOMEM);

  size_t mapped_size = page_align (gbl, len);

  if (flags & MAP_ANON)
    memset (result, 0, mapped_size);

  /* 2026: an anonymous mapping takes no data from fd (it used to read
     from it if fd was one of UnixLib's, and fail for other values with
     ARMEABISupport 1.08).  */
  if (fd == -1 || (flags & MAP_ANON))
    return result;

  /* 2026: an fd that isn't UnixLib's.  Shared memory is mapped by
     ARMEABISupport itself; a shared file (1.07 and later) is read in
     below, through read (), as a UnixLib file is.  */
  if (BADF (fd))
    {
      int type = __armeabi_fd_type (fd);
      if (type == ARMEABISUPPORT_FD_TYPE_SHM)
	return result;
      if (type != ARMEABISUPPORT_FD_TYPE_SHARED_FILE)
	{
	  munmap (result, mapped_size);
	  return (void *) __set_errno (type == ARMEABISUPPORT_FD_TYPE_INVALID
				       ? EBADF : ENODEV);
	}
    }

  /* Read in the relevant section of the file */
  /* FIXME: Take into account file/memory protections.  */
  int count = 0;

  __off64_t oldpos = lseek64 (fd, 0, SEEK_CUR);

  lseek64 (fd, FILE_OFFSET (offset), SEEK_SET);

  while (count < mapped_size)
    {
      ssize_t size = read (fd, result + count, mapped_size - count);

      if (size < 0)
	{
	  int save_errno = errno;
	  munmap (result, mapped_size);
	  lseek64 (fd, oldpos, SEEK_SET);
	  return (void *) __set_errno (save_errno);
	}
      else if (size == 0)
	break;

      count += size;
    }
  lseek64 (fd, oldpos, SEEK_SET);

  /* Ref: http://linux.die.net/man/2/mmap
   * "For a file that is not a multiple of the page size, the remaining memory
   * is zeroed when mapped, and writes to that region are not written out to
   * the file."
   */
  if (mapped_size - len > 0)
    memset (result + len, 0, mapped_size - len);

  return result;
}

/* Deallocate any mapping for the region starting at ADDR and extending LEN
   bytes.  Returns 0 if successful, -1 for errors (and sets errno).  */
int
munmap (void * addr, size_t len)
{
  PTHREAD_UNSAFE

  _kernel_oserror *err;
  void *mmap_addr;
  size_t size;
  off_t offset;
  int prot, flags, fd;

  err = _swix(ARMEABISupport_MMapOp, _INR(0,1)|_OUTR(0,5),
				     ARMEABISUPPORT_MMAPOP_GET_INFO,
				     addr,
				     &mmap_addr,
				     &size,
				     &prot,
				     &flags,
				     &fd,
				     &offset);
  /* We get an error if a specific block wasn't found, however, for a memory
   * mapping (rather than a file mapping), that isn't an issue.  */
  if (!err)
    {
      /* The start of a previously allocated block was passed in, is it a file
       * mapping?  */
      /* Only UnixLib's own files: ARMEABISupport writes back a shared
	 file (1.07 and later) itself, and shared memory needs nothing.  */
      if (fd != -1 && !BADF (fd)
	  && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE))
        {
	  /* Write back file contents if necessary
	     All the docs I've seen suggest that we only need to write
	     stuff back on msync(), but some software (e.g. mkimage)
	     seems to rely on the writeback occuring un munmap().  */
          /* Assume that the user only wants 'len' bytes writing.  */
	  __off64_t oldpos = lseek64 (fd, 0, SEEK_CUR);
	  lseek64 (fd, FILE_OFFSET (offset), SEEK_SET);
	  write (fd, addr, len);
	  lseek64 (fd, oldpos, SEEK_SET);
	}
    }

  err = _swix(ARMEABISupport_MMapOp, _INR(0,2), ARMEABISUPPORT_MMAPOP_UNMAP,
						addr,
						len);

  return err ? __ul_seterr (err, __errno_from_armeabisupport(err->errnum)) : 0;
}

/* Change the memory protection of the region starting at ADDR and
   extending LEN bytes to PROT.  Returns 0 if successful, -1 for errors
   (and sets errno).  */
int
mprotect (void * addr __attribute__ ((__unused__)),
	  size_t len __attribute__ ((__unused__)),
	  int prot __attribute__ ((__unused__)))
{
  struct ul_global *gbl = &__ul_global;

  if (!addr || (size_t)addr & (gbl->pagesize - 1))
    return EINVAL;

  _kernel_oserror *err;
  err = _swix(ARMEABISupport_MMapOp, _INR(0,3), ARMEABISUPPORT_MMAPOP_PROTECT,
						addr,
						len,
						prot);

  return err ? __ul_seterr (err, __errno_from_armeabisupport(err->errnum)) : 0;
}

/* this is supposed to be able to work on non-mmaped memory too. */

/* Synchronize the region starting at ADDR and extending LEN bytes with the
   file it maps.  Filesystem operations on a file being mapped are
   unpredictable before this is done.  */
int
msync (void * addr, size_t len, int syncflags)
{
  const struct ul_global *gbl = &__ul_global;

  PTHREAD_UNSAFE

  _kernel_oserror *err;
  void *mmap_addr;
  size_t size;
  off_t offset;
  int prot, flags, fd;

  FDOP_CALLER ();
  if ((size_t)addr & (gbl->pagesize - 1))
    return __set_errno (EINVAL);

  err = _swix(ARMEABISupport_MMapOp, _INR(0,1)|_OUTR(0,5),
				     ARMEABISUPPORT_MMAPOP_GET_INFO,
				     addr,
				     &mmap_addr,
				     &size,
				     &prot,
				     &flags,
				     &fd,
				     &offset);
  /* If we couldn't find the mmap mapping, then return an error.  */
  if (err)
    return __set_errno (EINVAL);

  /* Don't access areas outside the mapped range.  */
  if (addr + len > mmap_addr + size)
    return __set_errno (ENOMEM);

  if (fd == -1 || (BADF (fd) && !__armeabi_have_fdop ()))
    return __set_errno (EINVAL);

  /* If no length specified, flush the whole mapping.  */
  if (!len)
    {
      len = size;
      addr = mmap_addr;
    }

  /* 2026: a shared file or shared memory of ARMEABISupport 1.07 and
     later: it writes back what needs it.  */
  if (BADF (fd))
    {
      int type = __armeabi_fd_type (fd);
      if (type == ARMEABISUPPORT_FD_TYPE_SHM)
	return 0;	/* nothing to write back */
      if (type != ARMEABISUPPORT_FD_TYPE_SHARED_FILE)
	return __set_errno (EINVAL);
      err = _swix (ARMEABISupport_MMapOp, _INR(0,3),
		   ARMEABISUPPORT_MMAPOP_MSYNC, addr, len, syncflags);
      return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
		 : 0;
    }

  /* Only valid if this is a file open for non-private writing.  */
  if (!(prot & PROT_WRITE) || (flags & MAP_PRIVATE))
    return __set_errno (EINVAL);

  __off64_t oldpos = lseek64 (fd, 0, SEEK_CUR);
  lseek64 (fd, FILE_OFFSET (offset) + (addr - mmap_addr), SEEK_SET);
  int fail = write (fd, addr, len) != len;
  lseek64 (fd, oldpos, SEEK_SET);

  return fail ? __set_errno (EIO) : 0;
}

/* Advise the system about particular usage patterns the program follows
   for the region starting at ADDR and extending LEN bytes.  */
int
madvise (void * addr, size_t len, int advice)
{
  const struct ul_global *gbl = &__ul_global;

  if ((size_t)addr & (gbl->pagesize - 1))
    return __set_errno (EINVAL);

  int fd;
  _kernel_oserror *err;
  err = _swix(ARMEABISupport_MMapOp, _INR(0,1)|_OUT(4),
				     ARMEABISUPPORT_MMAPOP_GET_INFO,
				     addr,
				     &fd);
  /* For simplicity don't allow remapping of mapped files.  */
  if (err || (fd != -1 && !BADF(fd)))
    return __set_errno (ENOSYS);

  err = _swix(ARMEABISupport_MMapOp, _INR(0,3), ARMEABISUPPORT_MMAPOP_ADVISE,
						addr,
						len,
						advice);
  return err ? __ul_seterr (err, __errno_from_armeabisupport(err->errnum)) : 0;
}

/* Remap addresses mapped by the range [ADDR,ADDR+OLD_LEN) to new length
   NEW_LEN.  If MAY_MOVE is MREMAP_MAYMOVE the returned address may differ
   from ADDR.  The return value is the actual mapping address chosen or
   (void *) -1 for errors (in which case `errno' is set).  */

void *
mremap (void * addr, size_t old_len, size_t new_len, int may_move)
{
  _kernel_oserror *err;
  void *result;
  err = _swix(ARMEABISupport_MMapOp, _INR(0,4)|_OUT(0), ARMEABISUPPORT_MMAPOP_MREMAP,
							addr,
							old_len,
							new_len,
							may_move,
							&result);

  return err ? (void *)__ul_seterr (err, __errno_from_armeabisupport(err->errnum)) : result;
}

/* 2026: the lengths given to ftruncate for shared memory, so that fstat
   can give st_size: ARMEABISupport doesn't keep them (its ftruncate of
   shared memory does nothing; the size is set by mmap) and has no FdOp
   information for shared memory.  Only this program's ftruncate of that
   fd (or of the fd it was duplicated from) is known; otherwise 0 (so
   another program that opens the same object sees 0).  ARMEABISupport
   1.07 and later only: with 1.05, fstat of shared memory is EBADF, as it
   always was.  A few slots; 0 is never such an fd.  */
#define SHM_SIZES 16
static struct { int fd; long len; } shm_sizes[SHM_SIZES];
static unsigned shm_sizes_next;

static void
shm_size_set (int fd, long len)
{
  for (int i = 0; i < SHM_SIZES; i++)
    if (shm_sizes[i].fd == fd)
      {
	shm_sizes[i].len = len;
	return;
      }
  if (len == 0)
    return;
  /* A free slot (length 0) if there is one, or the oldest.  */
  int i;
  for (i = 0; i < SHM_SIZES; i++)
    if (shm_sizes[i].len == 0)
      break;
  if (i == SHM_SIZES)
    i = shm_sizes_next++ % SHM_SIZES;
  shm_sizes[i].fd = fd;
  shm_sizes[i].len = len;
}

static long
shm_size_get (int fd)
{
  for (int i = 0; i < SHM_SIZES; i++)
    if (shm_sizes[i].fd == fd)
      return shm_sizes[i].len;
  return 0;
}

int shm_open(const char *name, int oflag, mode_t mode)
{
  _kernel_oserror *err;
  int fd;
  err = _swix(ARMEABISupport_ShmOp, _INR(0,3)|_OUT(0), ARMEABISUPPORT_SHMOP_OPEN,
						       name,
						       oflag,
						       mode,
						       &fd);

  if (err)
    return __ul_seterr (err, __errno_from_armeabisupport (err->errnum));
  shm_size_set (fd, 0);
  return fd;
}

int shm_unlink(const char *name)
{
  if (strlen(name) > PATH_MAX)
    return ENAMETOOLONG;
  _kernel_oserror *err;

  err = _swix (ARMEABISupport_ShmOp, _INR(0,1), ARMEABISUPPORT_SHMOP_UNLINK,
					        name);

  return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum)) : 0;
}

int __errno_from_armeabisupport (int errnum)
{
  int error;
  switch (errnum)
  {
  case ARMEABISUPPORT_ERROR_EACCES:
    error = EACCES;
    break;
  case ARMEABISUPPORT_ERROR_EEXIST:
    error = EEXIST;
    break;
  case ARMEABISUPPORT_ERROR_EINVAL:
    error = EINVAL;
    break;
  case ARMEABISUPPORT_ERROR_ENAMETOOLONG:
    error = ENAMETOOLONG;
    break;
  case ARMEABISUPPORT_ERROR_ENOENT:
    error = ENOENT;
    break;
  case ARMEABISUPPORT_ERROR_ENOSPC:
    error = ENOSPC;
    break;
  case ARMEABISUPPORT_ERROR_EBADF:
    error = EBADF;
    break;
  case ARMEABISUPPORT_ERROR_ENOMEM:
    error = ENOMEM;
    break;
  case ARMEABISUPPORT_ERROR_EOVERFLOW:
    error = EOVERFLOW;
    break;
  default:
    error = EOPSYS;
    break;
  }

  return error;
}

/* 2026: file descriptors that belong to ARMEABISupport.

   ARMEABISupport 1.05 hands out shared memory fds that are pointers
   (IS_SHM_FD), closed and duplicated with ShmOp reasons 2 and 3.  1.07
   and later number all their fds (shared memory, and shared files opened
   with O_SHAREDFILE) from 1000 and work on them with FdOp; ShmOp reason 2
   is DUP there, and CLOSE has gone.  Lee Noar's GCCSDK UnixLib change of
   2026-11-01 moved UnixLib to the new interface; here both are kept, so a
   program works with either module.  Which one is loaded is found out
   once: 1.05 has no FdOp (its SWI handler, or the kernel without the
   module, says the SWI isn't known), 1.07 and later answer every FdOp,
   even for an fd that isn't theirs (EBADF).

   Each function is called only when BADF (fd).  */

static int fdop_state = -1;	/* -1 not known yet, 0 1.05, 1 FdOp */

#if __UNIXLIB_FDOP_TRACE
const void *__armeabi_fd_caller;

/* One line on stderr: what failed, on which fd, the error, and where
   close (), read () etc. were called from.  */
static void
fdop_trace (const char *what, int fd, int errnum, const char *errmess)
{
  /* Not again from inside the write (if stderr isn't open, write comes
     back here).  */
  static int busy;
  if (busy)
    return;
  busy = 1;
  char line[256];
  int n = snprintf (line, sizeof line,
		    "UnixLib FdOp: %s fd %d: error &%X %s (from &%08X)\n",
		    what, fd, errnum, errmess ? errmess : "",
		    (unsigned) __armeabi_fd_caller);
  if (n > 0)
    write (2, line, n < (int) sizeof line ? n : (int) sizeof line - 1);
  busy = 0;
}
#endif

int
__armeabi_have_fdop (void)
{
  if (fdop_state < 0)
    {
      const _kernel_oserror *err
	= _swix (ARMEABISupport_FdOp, _INR(0,1), ARMEABISUPPORT_FDOP_VALIDATE,
		 -1);
      /* FdOp's own answers for fd -1 (EBADF, or "process not known"
	 before the program has registered): it has FdOp.  Anything else
	 ("SWI not known", from the module or the kernel): it hasn't.  */
      fdop_state = (err == NULL
		    || err->errnum == ARMEABISUPPORT_ERROR_EBADF
		    || err->errnum == ARMEABISUPPORT_ERROR_BAD_PROCESS);
    }
  return fdop_state;
}

/* An error from FdOp: errno from it, and EBADF when ARMEABISupport
   doesn't know the program (so it has no fds of its own).  */
static int
fd_error_ (const _kernel_oserror *err, int fd, const char *what)
{
#if __UNIXLIB_FDOP_TRACE
  fdop_trace (what, fd, err->errnum, err->errmess);
#else
  (void) fd;
  (void) what;
#endif
  if (err->errnum == ARMEABISUPPORT_ERROR_BAD_PROCESS)
    return __set_errno (EBADF);
  return __ul_seterr (err, __errno_from_armeabisupport (err->errnum));
}
#define fd_error(err) fd_error_ (err, fd, __func__)

/* ARMEABISUPPORT_FD_TYPE_SHM, _SHARED_FILE or _IPC, or
   ARMEABISUPPORT_FD_TYPE_INVALID (errno EBADF) if it isn't one of
   ARMEABISupport's.  */
int
__armeabi_fd_type (int fd)
{
  if (!__armeabi_have_fdop ())
    {
      if (IS_SHM_FD (fd))
	return ARMEABISUPPORT_FD_TYPE_SHM;
      (void) __set_errno (EBADF);
      return ARMEABISUPPORT_FD_TYPE_INVALID;
    }

  int type;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,1) | _OUT(1),
	     ARMEABISUPPORT_FDOP_VALIDATE, fd, &type);
  if (err != NULL || type == ARMEABISUPPORT_FD_TYPE_INVALID)
    {
#if __UNIXLIB_FDOP_TRACE
      /* Not for UnixLib's own numbers (dup2 and others ask about them as
	 a matter of course).  */
      if (fd >= 1000)
	fdop_trace ("validate", fd, err ? err->errnum : 0,
		    err ? err->errmess : "not ARMEABISupport's");
#endif
      (void) __set_errno (EBADF);
      return ARMEABISUPPORT_FD_TYPE_INVALID;
    }
  return type;
}

int
__armeabi_fd_close (int fd)
{
  const _kernel_oserror *err;

  if (!__armeabi_have_fdop ())
    {
      if (!IS_SHM_FD (fd))
	return __set_errno (EBADF);
      err = _swix (ARMEABISupport_ShmOp, _INR(0,1),
		   ARMEABISUPPORT_SHMOP_CLOSE_105, fd);
      return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
		 : 0;
    }

  err = _swix (ARMEABISupport_FdOp, _INR(0,1), ARMEABISUPPORT_FDOP_CLOSE, fd);
  if (err)
    return fd_error (err);
  shm_size_set (fd, 0);
  return 0;
}

int
__armeabi_fd_read (int fd, void *buf, unsigned int n)
{
  if (!__armeabi_have_fdop ())
    return __set_errno (EBADF);

  int got;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
	     ARMEABISUPPORT_FDOP_READ, fd, buf, n, &got);
  return err ? fd_error (err) : got;
}

int
__armeabi_fd_write (int fd, const void *buf, unsigned int n)
{
  if (!__armeabi_have_fdop ())
    return __set_errno (EBADF);

  int put;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
	     ARMEABISUPPORT_FDOP_WRITE, fd, buf, n, &put);
  return err ? fd_error (err) : put;
}

long
__armeabi_fd_lseek (int fd, long offset, int whence)
{
  if (!__armeabi_have_fdop ())
    return __set_errno (EBADF);

  long pos;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
	     ARMEABISUPPORT_FDOP_SEEK, fd, offset, whence, &pos);
  return err ? fd_error (err) : pos;
}

int
__armeabi_fd_fstat (int fd, struct stat *buf)
{
  if (!__armeabi_have_fdop ())
    return __set_errno (EBADF);	/* as before */

  int type = __armeabi_fd_type (fd);
  if (type == ARMEABISUPPORT_FD_TYPE_INVALID)
    return -1;	/* EBADF */

  if (type != ARMEABISUPPORT_FD_TYPE_SHARED_FILE)
    {
      /* Shared memory, or an IPC channel: FdOp GETINFO is only for files
	 (EINVAL for these), so they are described here.  Shared memory
	 is a regular file, owner read and write, as on Linux, with the
	 length its ftruncate gave; an IPC channel a FIFO.  */
      const int shm = type == ARMEABISUPPORT_FD_TYPE_SHM;
      buf->st_dev = DEV_CUSTOM;
      buf->st_ino = (__ino_t) fd;
      __stat (shm ? 1 : 0, 0, 0, shm ? shm_size_get (fd) : 0, 0, buf);
      buf->st_mode = (shm ? S_IFREG : S_IFIFO) | S_IRUSR | S_IWUSR;
      buf->st_nlink = 1;
      return 0;
    }

  unsigned objtype, loadaddr, execaddr, objlen, attr;
  const char *filename;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,1) | _OUTR(0,5),
	     ARMEABISUPPORT_FDOP_GETINFO, fd,
	     &objtype, &filename, &loadaddr, &execaddr, &objlen, &attr);
  if (err != NULL)
    return fd_error (err);

  buf->st_dev = DEV_CUSTOM;
  /* Shared memory has no file name: number it by its fd.  */
  buf->st_ino = filename ? __get_file_ino (NULL, filename) : (__ino_t) fd;
  return __stat (objtype, loadaddr, execaddr, objlen, attr, buf);
}

int
__armeabi_fd_fsync (int fd)
{
  /* Shared memory: nothing to write (FdOp FSYNC is only for files, EINVAL
     for shared memory), so 0, as on Linux.  An IPC channel: EINVAL, as
     for a pipe on Linux.  */
  if (!__armeabi_have_fdop ())
    return __set_errno (EBADF);	/* as before */

  int type = __armeabi_fd_type (fd);
  if (type != ARMEABISUPPORT_FD_TYPE_SHARED_FILE)
    return type == ARMEABISUPPORT_FD_TYPE_SHM ? 0
	   : type == ARMEABISUPPORT_FD_TYPE_INVALID ? -1	/* EBADF */
	   : __set_errno (EINVAL);

  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,1), ARMEABISUPPORT_FDOP_FSYNC, fd);
  return err ? fd_error (err) : 0;
}

int
__armeabi_fd_ftruncate (int fd, long length)
{
  if (!__armeabi_have_fdop ())
    {
      /* As before: nothing to do for 1.05's shared memory.  */
      return IS_SHM_FD (fd) ? 0 : __set_errno (EBADF);
    }

  if (length < 0)
    return __set_errno (EINVAL);

  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,2), ARMEABISUPPORT_FDOP_FTRUNCATE,
	     fd, length);
  if (err)
    return fd_error (err);
  if (__armeabi_fd_type (fd) == ARMEABISUPPORT_FD_TYPE_SHM)
    shm_size_set (fd, length);
  return 0;
}

/* isatty for an fd that isn't UnixLib's: never a terminal; 0, with errno
   EBADF if it isn't ARMEABISupport's either.  */
int
__armeabi_fd_isatty (int fd)
{
  (void) __armeabi_fd_type (fd);
  return 0;
}

int
__armeabi_fd_fcntl (int fd, int cmd, va_list *ap)
{
  const _kernel_oserror *err;

  if (!__armeabi_have_fdop ())
    {
      /* As before, for 1.05's shared memory.  */
      if (!IS_SHM_FD (fd))
	return __set_errno (EBADF);
      switch (cmd)
	{
	case F_DUPFD:
	  {
	    int duplicate_fd;
	    err = _swix (ARMEABISupport_ShmOp, _INR(0,1) | _OUT(0),
			 ARMEABISUPPORT_SHMOP_DUP_105, fd, &duplicate_fd);
	    return err
	      ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
	      : duplicate_fd;
	  }
	case F_GETFD: case F_SETFD: case F_GETFL: case F_SETFL:
	case F_GETUNL: case F_SETUNL: case F_SETOWN:
	case F_GETLK: case F_SETLK: case F_SETLKW:
	  return 0;
	default:
	  return __set_errno (EINVAL);
	}
    }

  switch (cmd)
    {
    case F_DUPFD:
      {
	int lowest = va_arg (*ap, int);
	if (lowest < 0)
	  return __set_errno (EINVAL);
	if (lowest <= 1000)
	  {
	    /* ARMEABISupport gives the lowest free number of its own, all
	       from 1000.  */
	    int duplicate_fd;
	    err = _swix (ARMEABISupport_FdOp, _INR(0,1) | _OUT(0),
			 ARMEABISUPPORT_FDOP_DUP, fd, &duplicate_fd);
	    if (err)
	      return fd_error (err);
	    shm_size_set (duplicate_fd, shm_size_get (fd));
	    return duplicate_fd;
	  }
	/* The lowest free number from LOWEST, as POSIX has it.  */
	for (int n = lowest; n < lowest + 1024; n++)
	  if (_swix (ARMEABISupport_FdOp, _INR(0,1),
		     ARMEABISUPPORT_FDOP_VALIDATE, n) != NULL)
	    {
	      err = _swix (ARMEABISupport_FdOp, _INR(0,2),
			   ARMEABISUPPORT_FDOP_DUP2, fd, n);
	      if (err)
		return fd_error (err);
	      shm_size_set (n, shm_size_get (fd));
	      return n;
	    }
	return __set_errno (EMFILE);
      }
    case F_GETFD:
      {
	int cloexec;
	err = _swix (ARMEABISupport_FdOp, _INR(0,1) | _OUT(0),
		     ARMEABISUPPORT_FDOP_GETFD, fd, &cloexec);
	return err ? fd_error (err) : (cloexec ? 1 : 0);	/* as for UnixLib's */
      }
    case F_SETFD:
      {
	int arg = va_arg (*ap, int);
	err = _swix (ARMEABISupport_FdOp, _INR(0,2),
		     ARMEABISUPPORT_FDOP_SETFD, fd, arg != 0);
	return err ? fd_error (err) : 0;
      }
    case F_GETLK:
      {
	/* struct flock's type and whence are shorts: pass them in ints.  */
	struct flock *arg = va_arg (*ap, struct flock *);
	int type = arg->l_type, whence = arg->l_whence;
	long start = arg->l_start, len = arg->l_len;
	err = _swix (ARMEABISupport_FdOp, _INR(0,5) | _OUTR(2,5),
		     ARMEABISUPPORT_FDOP_GETLOCK, fd, type, whence, start, len,
		     &type, &whence, &start, &len);
	if (err != NULL)
	  return fd_error (err);
	arg->l_type = type;
	arg->l_whence = whence;
	arg->l_start = start;
	arg->l_len = len;
	return 0;
      }
    case F_SETLK:
    case F_SETLKW:
      {
	/* F_SETLKW doesn't wait: if the lock is held, it fails as F_SETLK
	   does.  Locks are advisory.  */
	struct flock *arg = va_arg (*ap, struct flock *);
	err = _swix (ARMEABISupport_FdOp, _INR(0,5),
		     ARMEABISUPPORT_FDOP_SETLOCK, fd, (int) arg->l_type,
		     (int) arg->l_whence, (long) arg->l_start,
		     (long) arg->l_len);
	return err ? fd_error (err) : 0;
      }
    case F_GETFL:
    case F_SETFL:
    case F_GETUNL:
    case F_SETUNL:
    case F_SETOWN:
      /* Validate the fd; nothing else to do, as for 1.05.  */
      return __armeabi_fd_type (fd) == ARMEABISUPPORT_FD_TYPE_INVALID ? -1 : 0;
    default:
      return __set_errno (EINVAL);
    }
}

/* dup2 (fd, fd2) for one of ARMEABISupport 1.07's fds: possible only to
   another number of its own (from 1000); 1 if done (or failed, with
   *result set), 0 to leave it to dup2 as before (ARMEABISupport 1.05, or
   fd isn't ARMEABISupport's).  */
int
__armeabi_fd_dup2 (int fd, int fd2, int *result)
{
  if (!__armeabi_have_fdop ()
      || __armeabi_fd_type (fd) == ARMEABISUPPORT_FD_TYPE_INVALID)
    return 0;

  if (fd2 < 1000)
    {
      /* A UnixLib number can't hold one of ARMEABISupport's fds.  */
      *result = __set_errno (EINVAL);
      return 1;
    }
  if (fd2 == fd)
    {
      *result = fd2;
      return 1;
    }

  const _kernel_oserror *err
    = _swix (ARMEABISupport_FdOp, _INR(0,2), ARMEABISUPPORT_FDOP_DUP2, fd, fd2);
  if (err)
    *result = fd_error (err);
  else
    {
      shm_size_set (fd2, shm_size_get (fd));
      *result = fd2;
    }
  return 1;
}

/* open with O_SHAREDFILE: through ARMEABISupport when it can (1.07 and
   later); -2 if it can't, for open to do an ordinary open instead.  */
int
__armeabi_open_shared (const char *ux_file, int oflag, int mode)
{
  if (!__armeabi_have_fdop ())
    return -2;

  char file[MAXPATHLEN];
  if (!__riscosify_std (ux_file, (oflag & (O_CREAT | O_WRONLY | O_RDWR)) != 0,
			file, sizeof (file), NULL))
    return __set_errno (ENAMETOOLONG);

  const char *ro_file = file;
#if __UNIXLIB_SYMLINKS
  char target[MAXPATHLEN];
  if (__resolve_symlinks (file, target, sizeof (target), 0) != 0)
    return -1;
  ro_file = target;
#endif

  int fd;
  const _kernel_oserror *err
    = _swix (ARMEABISupport_FileOp, _INR(0,3) | _OUT(0),
	     ARMEABISUPPORT_FILEOP_OPEN, ro_file, oflag & ~O_SHAREDFILE, mode,
	     &fd);
  return err ? fd_error (err) : fd;
}
