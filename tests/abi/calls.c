/* Which library symbols the stat, seek and truncate calls end up at.  */
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
int
use (FILE *f, struct stat *st, fpos_t *p)
{
  return stat ("a", st) + fstat (0, st) + lstat ("b", st)
	 + (int) lseek (0, 0, SEEK_SET) + fseeko (f, 0, SEEK_SET)
	 + (int) ftello (f) + fgetpos (f, p) + fsetpos (f, p)
	 + ftruncate (0, 0) + truncate ("c", 0)
	 + (mmap (0, 4096, PROT_READ, MAP_SHARED, 0, 0) != MAP_FAILED);
}
