/* UnixLib internal file descriptor interface definition.
   Copyright (c) 2002-2021 UnixLib Developers.  */

#ifdef __TARGET_SCL__
#  error "SCL has no fd support"
#endif

#ifndef __INTERNAL_FD_H
#define __INTERNAL_FD_H 1

#ifndef __UNIXLIB_FEATURES_H
#include <features.h>
#endif

#ifndef __UNIXLIB_TYPES_H
#include <unixlib/types.h>
#endif

__BEGIN_DECLS

#define getfd(fdes) ((struct __unixlib_fd *)(void *)(((char *)(__ul_global.sulproc->file_descriptors)) + ((fdes) * __ul_global.sulproc->fdsize)))

/* Beware of backwards compatibility when altering these structures.
   They are referenced by SUL and other processes */

/* Multiple __unixlib_fd structures may point to one of these */
struct __unixlib_fd_handle
{
  unsigned int refcount;
  unsigned int type; /* Device type (tty, socket etc. ), see DEV_* */
  void *handle; /* device specific field (i.e. socket, file handle) */
};

/* File descriptor structure definition. */
struct __unixlib_fd
{
  struct __unixlib_fd_handle *devicehandle;
  unsigned int dflag; /* File descriptor flags.  */
  int fflag; /* File status flags (attribs for opening a file) */
};

#ifdef __ARM_EABI__
/* A shared memory fd from ARMEABISupport 1.05, which is a pointer.
   1.07 and later number their fds from 1000: see __armeabi_have_fdop.  */
#define IS_SHM_FD(fd) ((fd) != -1 && (void *)(fd) > (void *)0x8000)

/* 2026: fds that aren't UnixLib's but may be ARMEABISupport's (shared
   memory, and from 1.07 shared files).  Each is called only when
   BADF (fd); with ARMEABISupport 1.05 they do what UnixLib did before
   (shared memory: close, dup and ftruncate; otherwise EBADF), with 1.07
   and later they call ARMEABISupport_FdOp.  In sys/mman-armeabi.c.  */
#include <stdarg.h>
struct stat;
extern int __armeabi_have_fdop (void);
extern int __armeabi_fd_type (int __fd);
extern int __armeabi_fd_close (int __fd);
extern int __armeabi_fd_read (int __fd, void *__buf, unsigned int __n);
extern int __armeabi_fd_write (int __fd, const void *__buf, unsigned int __n);
extern long __armeabi_fd_lseek (int __fd, long __offset, int __whence);
extern int __armeabi_fd_fstat (int __fd, struct stat *__buf);
extern int __armeabi_fd_fsync (int __fd);
extern int __armeabi_fd_ftruncate (int __fd, long __length);
extern int __armeabi_fd_isatty (int __fd);
extern int __armeabi_fd_fcntl (int __fd, int __cmd, va_list *__ap);
extern int __armeabi_fd_dup2 (int __fd, int __fd2, int *__result);
extern int __armeabi_open_shared (const char *__file, int __oflag, int __mode);

/* 2026: a diagnostic build (CFLAGS -D__UNIXLIB_FDOP_TRACE=1, never a
   release) writes a line to stderr for each FdOp call on these fds that
   fails, with the address close (), read () etc. were called from.  The
   address is kept in __armeabi_fd_caller, set as each helper is called
   (not per thread: good enough for a diagnostic).  sys/mman-armeabi.c
   itself defines __UNIXLIB_FDOP_INTERNAL so its own calls don't set it.  */
#ifndef __UNIXLIB_FDOP_TRACE
#  define __UNIXLIB_FDOP_TRACE 0
#endif
#if __UNIXLIB_FDOP_TRACE && !defined __UNIXLIB_FDOP_INTERNAL
extern const void *__armeabi_fd_caller;
#  define __ARMEABI_CALLED(call) \
     (__armeabi_fd_caller = __builtin_return_address (0), call)
#  define __armeabi_fd_type(fd) __ARMEABI_CALLED (__armeabi_fd_type (fd))
#  define __armeabi_fd_close(fd) __ARMEABI_CALLED (__armeabi_fd_close (fd))
#  define __armeabi_fd_read(fd, b, n) \
     __ARMEABI_CALLED (__armeabi_fd_read (fd, b, n))
#  define __armeabi_fd_write(fd, b, n) \
     __ARMEABI_CALLED (__armeabi_fd_write (fd, b, n))
#  define __armeabi_fd_lseek(fd, o, w) \
     __ARMEABI_CALLED (__armeabi_fd_lseek (fd, o, w))
#  define __armeabi_fd_fstat(fd, b) __ARMEABI_CALLED (__armeabi_fd_fstat (fd, b))
#  define __armeabi_fd_fsync(fd) __ARMEABI_CALLED (__armeabi_fd_fsync (fd))
#  define __armeabi_fd_ftruncate(fd, l) \
     __ARMEABI_CALLED (__armeabi_fd_ftruncate (fd, l))
#  define __armeabi_fd_isatty(fd) __ARMEABI_CALLED (__armeabi_fd_isatty (fd))
#  define __armeabi_fd_fcntl(fd, c, a) \
     __ARMEABI_CALLED (__armeabi_fd_fcntl (fd, c, a))
#  define __armeabi_fd_dup2(fd, f2, r) \
     __ARMEABI_CALLED (__armeabi_fd_dup2 (fd, f2, r))
#endif
#endif

/* Macro for checking file descriptor validity.  */
#define BADF(fdes) ((unsigned int)(fdes) >= __ul_global.sulproc->maxfd || getfd (fdes)->devicehandle == NULL)

/* File descriptor flags.  */

/* Set if file is a directory.  */
#define FILE_ISDIR		0x0001
/* Set if handle in devicehandle is from OS (OS_ChangeRedirection).  */
#define FILE_HANDLE_FROM_OS	0x0002

/* Allocate and initialise a new file descriptor, searching from fd start
   onwards.  */
extern int __alloc_file_descriptor (int start);

/* Open a file descriptor with given filename.  Returns the file descriptor
   on success, -1 on failure.  */
extern int __open_fn (int __fd, const char *__file, int __oflag, int __mode);

/* Open a file descriptor with given RISC OS file handle.  Returns the file
   descriptor on success, -1 on failure. UnixLib will never close this
   handle, only read/write from it.  */
extern int __open_fh (int __fd, int __fh, int __oflag, int __mode);

/* Close a file descriptor.  Returns zero on success, -1 on failure.  */
extern int __close (struct __unixlib_fd *file_desc);

/* Re-open a file descriptor.  Returns the file descriptor on success,
   -1 on failure.  */
extern int __reopen (int __fd, const char *__file, int __oflag, ...);

__END_DECLS

#endif
