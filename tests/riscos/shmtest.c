/* shmtest: POSIX shared memory and ARMEABISupport's fds (riscos-unixlib
   Q1).  Works with ARMEABISupport 1.05 and 1.07+; says which it found.
   With 1.07+ it also tries a shared file (O_SHAREDFILE).  */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

extern int __armeabi_have_fdop (void);

static int bad;

static void
result (const char *what, int ok)
{
  printf ("%-48s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    printf ("  (errno %d: %s)\n", errno, strerror (errno));
  bad |= !ok;
}

int
main (void)
{
  int fdop = __armeabi_have_fdop ();
  printf ("ARMEABISupport: %s\n", fdop ? "1.07 or later (FdOp)" : "1.05 or earlier");

  /* Bad fds: EBADF, as always.  */
  errno = 0;
  result ("close(200): -1, EBADF", close (200) == -1 && errno == EBADF);
  errno = 0;
  result ("isatty(200): 0, EBADF", isatty (200) == 0 && errno == EBADF);

  /* Shared memory: open, size, map, write, read back through a second
     mapping, close, unlink.  */
  shm_unlink ("/unixlibtest");
  int fd = shm_open ("/unixlibtest", O_RDWR | O_CREAT, 0600);
  result ("shm_open", fd != -1);
  if (fd == -1)
    goto done;
  result ("ftruncate to 8192", ftruncate (fd, 8192) == 0);
  char *a = mmap (NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  result ("mmap", a != MAP_FAILED);
  if (a != MAP_FAILED)
    {
      strcpy (a, "hello from UnixLib");
      char *b = mmap (NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      result ("second mmap sees the same memory",
	      b != MAP_FAILED && strcmp (b, "hello from UnixLib") == 0);
      if (b != MAP_FAILED)
	munmap (b, 8192);
      result ("msync", msync (a, 8192, MS_SYNC) == 0 || !fdop);
      result ("munmap", munmap (a, 8192) == 0);
    }
  int d = dup (fd);
  result ("dup", d != -1);
  if (fdop)
    {
      struct stat st;
      result ("fstat (1.07+)", fstat (fd, &st) == 0);
      result ("F_SETFD/F_GETFD close-on-exec (1.07+)",
	      fcntl (fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl (fd, F_GETFD) == 1);
    }
  result ("close the dup", d == -1 || close (d) == 0);
  result ("close", close (fd) == 0);
  errno = 0;
  result ("close again: EBADF", close (fd) == -1 && errno == EBADF);
  result ("shm_unlink", shm_unlink ("/unixlibtest") == 0);

  /* A shared file: with 1.05 an ordinary file.  */
  {
    const char *name = "/<Wimp$ScrapDir>/unixlibsh";
    int sf = open (name, O_RDWR | O_CREAT | O_TRUNC | O_SHAREDFILE, 0600);
    result (fdop ? "open O_SHAREDFILE (1.07+: ARMEABISupport's)"
		 : "open O_SHAREDFILE (1.05: an ordinary open)", sf != -1);
    if (sf != -1)
      {
	result ("  fd is ARMEABISupport's (>= 1000) on 1.07+",
		fdop ? sf >= 1000 : sf < 1000);
	result ("  write", write (sf, "0123456789", 10) == 10);
	result ("  lseek to 4", lseek (sf, 4, SEEK_SET) == 4);
	char buf[8] = { 0 };
	result ("  read back", read (sf, buf, 3) == 3 && memcmp (buf, "456", 3) == 0);
	struct flock fl = { F_WRLCK, SEEK_SET, 0, 5, 0 };
	if (fdop)
	  result ("  F_SETLK (1.07+)", fcntl (sf, F_SETLK, &fl) == 0);
	result ("  close", close (sf) == 0);
	remove (name);
      }
  }

done:
  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
