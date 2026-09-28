/* Sizes and offsets that compiled programs and libraries depend on.
   check.sh compiles this in several modes and compares the results with
   expected.txt (UnixLib 5.0.1, plus the 5.0.2 struct stat64).  */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#define SZ(name, expr) char name[expr];
SZ(sz_stat, sizeof (struct stat))
SZ(off_stat_size, offsetof (struct stat, st_size) + 1)
SZ(off_stat_blksize, offsetof (struct stat, st_blksize) + 1)
SZ(sz_off_t, sizeof (off_t))
SZ(sz_fpos_t, sizeof (fpos_t))
SZ(sz_FILE, sizeof (FILE))
SZ(off_FILE_offset, offsetof (FILE, __offset) + 1)
#ifdef __USE_LARGEFILE64
SZ(sz_stat64, sizeof (struct stat64))
SZ(off_stat64_size, offsetof (struct stat64, st_size) + 1)
#endif
