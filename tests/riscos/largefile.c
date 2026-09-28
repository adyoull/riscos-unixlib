/* largefile: files over 2GB with the 64-bit file interface.

   RISC OS file pointers are unsigned 32-bit, so files can be up to 4GB-1
   bytes (FileCore on RISC OS 5, DOSFS/FAT32).  Built with
   -D_FILE_OFFSET_BITS=64, off_t is 64-bit and lseek, fstat, fseeko... reach
   past 2GB.  Programs built without it are unchanged.

     largefile [-mb N] [file]
   Creates FILE (default LargeTest in the current directory) of N MB
   (default 3072 = 3GB), checks seeking, sizes and data past 2GB with the
   POSIX and stdio calls, then deletes it.  Needs that much free space on a
   filing system that supports files over 2GB, and some minutes (RISC OS
   fills the extended file with zeros).  */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined _FILE_OFFSET_BITS || _FILE_OFFSET_BITS != 64
#error "build with -D_FILE_OFFSET_BITS=64"
#endif

static int fails;

#define CHECK(c, ...) do { if (c) printf ("ok    "); \
  else { printf ("FAIL  "); fails++; } \
  printf (__VA_ARGS__); printf ("\n"); } while (0)

static int
write_at (int fd, off_t pos, const char *s)
{
  return lseek (fd, pos, SEEK_SET) == pos
	 && write (fd, s, strlen (s)) == (ssize_t) strlen (s);
}

static int
read_at (int fd, off_t pos, const char *s)
{
  char buf[32] = "";
  size_t n = strlen (s);
  return lseek (fd, pos, SEEK_SET) == pos && read (fd, buf, n) == (ssize_t) n
	 && memcmp (buf, s, n) == 0;
}

int
main (int argc, char **argv)
{
  const char *name = "LargeTest";
  long mb = 3072;
  int i;

  for (i = 1; i < argc; i++)
    if (!strcmp (argv[i], "-mb") && i + 1 < argc)
      mb = atol (argv[++i]);
    else
      name = argv[i];

  const off_t size = (off_t) mb << 20;
  const off_t mark1 = ((off_t) 2 << 30) + 12345;	/* just past 2GB */
  const off_t mark2 = size - 8;				/* the last bytes */

  printf ("largefile: %s, %ld MB (sizeof (off_t) = %d)\n", name, mb,
	  (int) sizeof (off_t));
  if (size <= mark1 || size > 0xFFFFFFFFLL)
    {
      printf ("size must be over 2GB and under 4GB\n");
      return 1;
    }

  int fd = open (name, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      printf ("FAIL  open %s: %s\n", name, strerror (errno));
      return 1;
    }

  printf ("extending to %ld MB (this can take minutes)...\n", mb);
  fflush (stdout);
  CHECK (ftruncate (fd, size) == 0, "ftruncate to %lld: %s", (long long) size,
	 strerror (errno));
  CHECK (write_at (fd, mark1, "MARK-ONE"), "write at %lld", (long long) mark1);
  CHECK (write_at (fd, mark2, "MARK-TWO"), "write at %lld", (long long) mark2);

  struct stat st;
  CHECK (fstat (fd, &st) == 0 && st.st_size == size,
	 "fstat size %lld (want %lld)", (long long) st.st_size,
	 (long long) size);
  CHECK (lseek (fd, 0, SEEK_END) == size, "lseek SEEK_END = size");
  CHECK (lseek (fd, -8, SEEK_CUR) == mark2, "lseek SEEK_CUR back 8");
  CHECK (read_at (fd, mark1, "MARK-ONE"), "read back at %lld",
	 (long long) mark1);
  CHECK (read_at (fd, mark2, "MARK-TWO"), "read back at %lld",
	 (long long) mark2);
  errno = 0;
  CHECK (lseek (fd, (off_t) 1 << 32, SEEK_SET) == -1 && errno == EOVERFLOW,
	 "lseek to 4GB fails with EOVERFLOW");
  errno = 0;
  CHECK (ftruncate (fd, (off_t) 5 << 30) == -1 && errno == EFBIG,
	 "ftruncate to 5GB fails with EFBIG");
  close (fd);

  CHECK (stat (name, &st) == 0 && st.st_size == size, "stat size %lld",
	 (long long) st.st_size);

  /* stdio */
  FILE *f = fopen (name, "rb");
  char buf[16] = "";
  CHECK (f != NULL, "fopen");
  if (f)
    {
      CHECK (fseeko (f, mark1, SEEK_SET) == 0 && fread (buf, 1, 8, f) == 8
	     && memcmp (buf, "MARK-ONE", 8) == 0, "fseeko + fread past 2GB");
      CHECK (ftello (f) == mark1 + 8, "ftello %lld", (long long) ftello (f));
      fpos_t pos;
      CHECK (fgetpos (f, &pos) == 0, "fgetpos");
      CHECK (fseeko (f, -8, SEEK_END) == 0 && fread (buf, 1, 8, f) == 8
	     && memcmp (buf, "MARK-TWO", 8) == 0, "fseeko SEEK_END + fread");
      CHECK (fsetpos (f, &pos) == 0 && ftello (f) == mark1 + 8,
	     "fsetpos back to %lld", (long long) (mark1 + 8));
      CHECK (fseeko (f, -8, SEEK_CUR) == 0 && fread (buf, 1, 8, f) == 8
	     && memcmp (buf, "MARK-ONE", 8) == 0, "fseeko SEEK_CUR");
      fclose (f);
    }

  CHECK (unlink (name) == 0, "delete %s", name);
  printf ("%s\n", fails ? "FAILED" : "PASS");
  return fails != 0;
}
