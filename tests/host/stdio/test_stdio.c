/* Host test for stdio/fread.c and stdio/fwrite.c (2026, C4): after a
   short read () or write (), the next one continues after the bytes
   already transferred.  (A write () that takes nothing still loops, as
   before: that is left alone, see MODIFICATIONS.md C4.)
   Before, a short read stored the rest over the bytes it had (right count,
   wrong data) and a short write wrote the same bytes again.  */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <internal/unix.h>

struct ul_global __ul_global;
int __set_errno (int e) { (void) e; return -1; }
int __validfp (FILE *f) { (void) f; return 1; }
int __flslbbuf (void) { return 0; }
int __flsbuf (int c, FILE *f) { (void) c; (void) f; return 0; }

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* A "device" that transfers at most CHUNK bytes per call.  */
static unsigned char src[100], dst[100];
static size_t src_pos, dst_pos, chunk;
static int writes;

ssize_t read (int fd, void *buf, size_t n)
{
  (void) fd;
  if (n > chunk) n = chunk;
  if (n > sizeof src - src_pos) n = sizeof src - src_pos;
  memcpy (buf, src + src_pos, n);
  src_pos += n;
  return n;
}

ssize_t write (int fd, const void *buf, size_t n)
{
  (void) fd;
  if (++writes > 50)
    return -1;			/* stop a runaway loop */
  if (n > chunk) n = chunk;
  memcpy (dst + dst_pos, buf, n);
  dst_pos += n;
  return n;
}

int isatty (int fd) { (void) fd; return 0; }

int main (void)
{
  FILE f;
  unsigned char buf[100];
  size_t i;

  for (i = 0; i < sizeof src; i++)
    src[i] = (unsigned char) i;

  /* Unbuffered read, 30 bytes per read ().  */
  memset (&f, 0, sizeof f);
  f.__mode.__bits.__read = 1;
  chunk = 30; src_pos = 0;
  memset (buf, 0xEE, sizeof buf);
  CHECK (fread (buf, 1, 100, &f) == 100);
  CHECK (memcmp (buf, src, 100) == 0);
  CHECK (f.__offset == 100);

  /* Unbuffered write, 30 bytes per write ().  */
  memset (&f, 0, sizeof f);
  f.__mode.__bits.__write = 1;
  f.fd = 3;
  dst_pos = 0; writes = 0; memset (dst, 0, sizeof dst);
  CHECK (fwrite (src, 1, 100, &f) == 100);
  CHECK (memcmp (dst, src, 100) == 0);

  /* Buffered stream, direct write of a block bigger than the buffer.  */
  unsigned char obuf[16];
  memset (&f, 0, sizeof f);
  f.__mode.__bits.__write = 1;
  f.fd = 3; f.o_base = f.o_ptr = obuf; f.o_cnt = sizeof obuf;
  dst_pos = 0; writes = 0; memset (dst, 0, sizeof dst);
  CHECK (fwrite (src, 1, 100, &f) == 100);
  CHECK (memcmp (dst, src, 100) == 0);

  printf ("stdio: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
