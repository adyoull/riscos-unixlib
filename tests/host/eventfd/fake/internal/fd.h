/* Host test shim: the two structures as in incl-local/internal/fd.h.  */
struct __unixlib_fd_handle
{
  unsigned int refcount;
  unsigned int type;
  void *handle;
};
struct __unixlib_fd
{
  struct __unixlib_fd_handle *devicehandle;
  unsigned int dflag;
  int fflag;
};
extern int __alloc_file_descriptor (int);
extern struct __unixlib_fd *getfd (int);
