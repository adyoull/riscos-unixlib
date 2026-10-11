/* Run by scanf_test.py in an ARM emulator: sscanf with each length
   modifier, into fields pre-filled with 0x5A, on the real target (32-bit
   long), so a store of the wrong size shows.  */
#include <stdio.h>
#include <string.h>

struct res
{
  int r[16];
  long long ll[6];
  unsigned char hh[4];
  short s[2];
  int i[4];
  double ld;		/* long double is double here */
  void *pp[2];
  int guard[2];
} R;

void
t_main (void)
{
  memset (&R, 0x5A, sizeof R);
  R.r[0] = sscanf ("-9000000000", "%lld", &R.ll[0]);
  R.r[1] = sscanf ("ffffffffffffffff", "%llx", &R.ll[1]);
  R.r[2] = sscanf ("-5", "%hhd", &R.hh[0]);
  R.r[3] = sscanf ("123456789012", "%jd", &R.ll[2]);
  R.r[4] = sscanf ("4096", "%zu", &R.i[0]);
  R.r[5] = sscanf ("-77", "%td", &R.i[1]);
  R.r[6] = sscanf ("8000000000", "%qd", &R.ll[3]);
  R.r[7] = sscanf ("abcde", "abcde%lln", &R.ll[4]);
  R.r[8] = sscanf ("123", "%*d%hhn", &R.hh[2]);
  R.r[9] = sscanf ("7 8", "%ld %hd", &R.i[3], &R.s[0]);
  R.r[10] = sscanf ("1.5", "%Lf", &R.ld);
  R.r[11] = sscanf ("0x77", "%llp", &R.pp[1]);	/* a pointer, not 8 bytes */
  R.r[12] = sscanf ("5", "%Ld", &R.i[2]);	/* L on an integer: as before (int) */
}
