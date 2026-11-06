/* Entry points for tests/emu/fdop_test.py: the real close(), read(),
   fcntl() etc. (linked from build/work/build/.libs/libunixlib.a) on fds
   that aren't UnixLib's, run in the Unicorn ARM emulator with
   ARMEABISupport faked, as 1.05 and as 1.08.  Not a RISC OS program.  */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <internal/fd.h>
#include <internal/unix.h>
#include <internal/dev.h>
#include <pthread.h>

extern __off64_t lseek64 (int fd, __off64_t offset, int whence);

static struct __sul_process proc;
static struct __unixlib_fd fds[8];	/* all closed */
static struct __pthread_thread thread;

/* BADF needs a process with a (here empty) fd table, and __set_errno a
   running thread.  */
void
t_init (void)
{
  memset (fds, 0, sizeof fds);
  proc.maxfd = 8;
  proc.fdsize = sizeof (struct __unixlib_fd);
  proc.file_descriptors = fds;
  __ul_global.sulproc = &proc;
  __ul_global.pagesize = 4096;
  __ul_global.pthread_system_running = 0;
  __pthread_running_thread = &thread;
  errno = 0;
}

int t_errno (void) { return errno; }
int t_have_fdop (void) { return __armeabi_have_fdop (); }
int t_close (int fd) { return close (fd); }
int t_dup (int fd) { return dup (fd); }
int t_read (int fd, void *buf, int n) { return read (fd, buf, n); }
int t_write (int fd, const void *buf, int n) { return write (fd, buf, n); }
int t_lseek (int fd, int off, int whence) { return lseek (fd, off, whence); }
int t_fsync (int fd) { return fsync (fd); }
int t_ftruncate (int fd, int len) { return ftruncate (fd, len); }
int t_isatty (int fd) { return isatty (fd); }
int t_getfd (int fd) { return fcntl (fd, F_GETFD); }
int t_setfd (int fd, int v) { return fcntl (fd, F_SETFD, v); }
int t_getfl (int fd) { return fcntl (fd, F_GETFL); }
int t_shm_open (void) { return shm_open ("/t", O_RDWR | O_CREAT, 0600); }
int t_open_shared (void) { return __armeabi_open_shared ("x", O_RDWR | O_SHAREDFILE, 0); }

/* lseek64 beyond 32 bits: -1 and EOVERFLOW for these fds.  */
int
t_lseek64_big (int fd)
{
  return (int) lseek64 (fd, 0x100000000LL, SEEK_SET);
}

/* fstat: the size, or -1.  */
int
t_fstat_size (int fd)
{
  struct stat st;
  if (fstat (fd, &st) != 0)
    return -1;
  return (int) st.st_size;
}

/* struct flock with guard words around it: F_GETLK must fill the shorts
   without writing past them.  Returns the fcntl result; the struct is at
   *out (guard, flock, guard).  */
struct guarded_flock
{
  unsigned int before;
  struct flock fl;
  unsigned int after;
};
static struct guarded_flock gfl;

struct guarded_flock *
t_getlk (int fd)
{
  memset (&gfl, 0, sizeof gfl);
  gfl.before = 0xAAAAAAAA;
  gfl.after = 0xBBBBBBBB;
  gfl.fl.l_type = F_WRLCK;
  gfl.fl.l_whence = SEEK_SET;
  gfl.fl.l_start = 10;
  gfl.fl.l_len = 20;
  gfl.fl.l_pid = 0x5555;
  gfl.before = fcntl (fd, F_GETLK, &gfl.fl) == 0 ? gfl.before : 0;
  return &gfl;
}

int
t_setlk (int fd)
{
  struct flock fl = { F_RDLCK, SEEK_CUR, 100, 200, 0 };
  return fcntl (fd, F_SETLK, &fl);
}

/* mmap of LEN bytes of fd: the address, or 0 (MAP_FAILED).  */
void *
t_mmap (int fd, int len)
{
  void *p = mmap (NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  return p == MAP_FAILED ? NULL : p;
}

int t_munmap (void *p, int len) { return munmap (p, len); }
int t_dup2 (int fd, int fd2) { return dup2 (fd, fd2); }
int t_dupfd (int fd, int lowest) { return fcntl (fd, F_DUPFD, lowest); }
int t_setlkw (int fd)
{
  struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };
  return fcntl (fd, F_SETLKW, &fl);
}
void *
t_mmap_anon (int fd)
{
  void *p = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, fd, 0);
  return p == MAP_FAILED ? NULL : p;
}

/* A UnixLib fd 3 (a tty): isatty and fcntl must stay UnixLib's.  */
static struct __unixlib_fd_handle tty = { 1, 0, 0 };
int
t_unixlib_fd (void)
{
  tty.type = DEV_TTY;
  fds[3].devicehandle = &tty;
  int r = isatty (3) == 1 && fcntl (3, F_GETFD) == 0;
  fds[3].devicehandle = 0;
  return r;
}
int t_msync (void *p, int len) { return msync (p, len, MS_SYNC); }
