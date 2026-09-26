#include <sys/types.h>
struct __unixlib_fd_handle { unsigned int type; void *handle; };
struct __unixlib_fd { struct __unixlib_fd_handle *devicehandle; unsigned dflag; int fflag; };
void *__dspopen (struct __unixlib_fd *, const char *, int);
int __dspclose (struct __unixlib_fd *);
int __dspwrite (struct __unixlib_fd *, const void *, int);
int __dspioctl (struct __unixlib_fd *, unsigned long, void *);
void __dsp_exit (void);
