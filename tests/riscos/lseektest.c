/* lseektest: lseek past the end of a file (riscos-unixlib 5.0.3.9, from
   the Web Browser: SQLite reads 16 bytes at offset 24 of a new database,
   and on RISC OS the lseek made the file 24 zero bytes, so it was "not a
   database" ever after).  POSIX: lseek doesn't change the file, a read
   there gives end of file, and a write fills the gap with zeros.  Uses
   <Wimp$ScrapDir> or, given an argument, that directory (Unix form), so
   it can be run on several filing systems.  */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int bad;

static void
ok (int cond, const char *what)
{
  printf ("%-58s %s\n", what, cond ? "ok" : "WRONG");
  if (!cond)
    bad = 1;
}

/* The size of an open file (fstat reads its extent).  */
static long
size_of (int fd)
{
  struct stat st;
  return fstat (fd, &st) == 0 ? (long) st.st_size : -1;
}

/* The size of a closed file.  */
static long
size_of_file (const char *f)
{
  struct stat st;
  return stat (f, &st) == 0 ? (long) st.st_size : -1;
}

int
main (int argc, char **argv)
{
  char f[256];
  snprintf (f, sizeof f, "%s/lseektest",
	    argc > 1 ? argv[1] : "/<Wimp$ScrapDir>");
  remove (f);
  printf ("File %s\n", f);

  int fd = open (f, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      perror (f);
      return 1;
    }
  char buf[32];
  ok (lseek (fd, 24, SEEK_SET) == 24, "lseek 24 in a new file gives 24");
  ok (read (fd, buf, 16) == 0, "a read of 16 there gives end of file");
  ok (size_of (fd) == 0, "the file is still empty");
  ok (lseek (fd, 0, SEEK_CUR) == 24, "lseek SEEK_CUR 0 still gives 24");
  ok (write (fd, "X", 1) == 1, "a write of 1 byte there");
  ok (size_of (fd) == 25, "the file is now 25 bytes");
  ok (lseek (fd, 0, SEEK_CUR) == 25, "position 25");
  memset (buf, 1, sizeof buf);
  ok (lseek (fd, 0, SEEK_SET) == 0 && read (fd, buf, 32) == 25
      && buf[0] == 0 && buf[23] == 0 && buf[24] == 'X',
      "the gap is zeros, then the X");
  ok (lseek (fd, 100, SEEK_END) == 125 && size_of (fd) == 25,
      "lseek SEEK_END + 100: 125, the file not extended");
  ok (lseek (fd, -50, SEEK_CUR) == 75 && size_of (fd) == 25,
      "lseek SEEK_CUR - 50: 75, still not extended");
  ok (lseek (fd, 10, SEEK_SET) == 10 && read (fd, buf, 4) == 4,
      "back inside the file, a read works");
  close (fd);
  ok (size_of_file (f) == 25, "closed: 25 bytes");

  /* SQLite's way (no pread in UnixLib, so lseek and read): a new
     database file.  */
  remove (f);
  fd = open (f, O_RDWR | O_CREAT, 0644);
  ok (fd >= 0 && lseek (fd, 24, SEEK_SET) == 24 && read (fd, buf, 16) == 0
      && size_of (fd) == 0,
      "lseek 24, read 16 in a new file: end of file, still empty");
  close (fd);
  ok (size_of_file (f) == 0, "closed: still empty");

  /* stdio: fseek past the end, then fputc.  */
  remove (f);
  FILE *fp = fopen (f, "w+");
  ok (fp && fseek (fp, 8, SEEK_SET) == 0 && ftell (fp) == 8,
      "fseek 8 in a new stdio file, ftell 8");
  ok (fp && fputc ('Y', fp) == 'Y' && fclose (fp) == 0 && size_of_file (f) == 9,
      "fputc there, closed: 9 bytes");
  remove (f);

  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
