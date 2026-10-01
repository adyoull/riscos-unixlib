/* A heap past 128 MB (riscos-unixlib 5.0.3.1-rc3).

   RISC OS 5 gives a dynamic area at most 128 MB, so a heap in one area
   stopped there.  UnixLib now carries on in more areas ("<name> 2"...).

   heaptest        allocates 16 MB blocks up to 320 MB (or until memory
                   runs out), fills and checks them, lists the heap's
                   areas, tries one 128 MB + 64 KB block, frees it all.
                   PASS if it got past 160 MB (more than one area) and
                   every block kept its contents.
   heaptest -check run after the first has quit: PASS if none of its
                   areas are left.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

const char *const __dynamic_da_name = "UnixLibTest Heap";
int __dynamic_da_max_size = 512 << 20;

#define BLOCK (16 << 20)
#define MAXBLOCKS 20

static int
list_areas (int quiet)
{
  int area = -1, n = 0;
  for (;;)
    {
      _kernel_oserror *e;
      int size, max;
      const char *name;
      e = _swix (OS_DynamicArea, _INR(0,1) | _OUT(1), 3, area, &area);
      if (e || area == -1)
	break;
      if (_swix (OS_DynamicArea, _INR(0,1) | _OUT(2) | _OUT(5) | _OUT(8),
		 2, area, &size, &max, &name))
	continue;
      if (strncmp (name, "UnixLibTest Heap", 16) == 0)
	{
	  n++;
	  if (!quiet)
	    printf ("  area %d \"%s\": %d K of %d K\n", area, name,
		    size / 1024, max / 1024);
	}
      else if (!quiet && strncmp (name, "mmap#", 5) == 0)
	printf ("  (also \"%s\": %d K)\n", name, size / 1024);
    }
  return n;
}

int
main (int argc, char **argv)
{
  static unsigned char *b[MAXBLOCKS];
  int i, got = 0, ok = 1, areas;
  void *big;

  if (argc > 1 && strcmp (argv[1], "-check") == 0)
    {
      areas = list_areas (0);
      printf ("%s: %d UnixLibTest Heap areas left after exit\n",
	      areas ? "FAIL" : "PASS", areas);
      return areas != 0;
    }

  for (i = 0; i < MAXBLOCKS; i++)
    {
      b[i] = malloc (BLOCK);
      if (!b[i])
	break;
      memset (b[i], i + 1, BLOCK);
      got++;
      printf ("\r%d MB", got * 16);
      fflush (stdout);
    }
  printf ("\nallocated %d MB in 16 MB blocks\n", got * 16);
  for (i = 0; i < got; i++)
    {
      size_t j;
      for (j = 0; j < BLOCK; j += 4093)
	if (b[i][j] != (unsigned char) (i + 1))
	  {
	    printf ("block %d changed at %u\n", i, (unsigned) j);
	    ok = 0;
	    break;
	  }
    }
  areas = list_areas (0);
  printf ("%d heap areas\n", areas);

  big = malloc ((128 << 20) + (64 << 10));
  printf ("one 128 MB + 64 KB block: %s\n",
	  big ? "allocated" : "refused (RISC OS gave no area that big)");
  if (big)
    {
      memset (big, 0x5a, (128 << 20) + (64 << 10));
      list_areas (0);
    }
  free (big);
  for (i = 0; i < got; i++)
    free (b[i]);

  if (got * 16 > 160 && areas > 1 && ok)
    printf ("PASS (now run HeapCheck after this has quit)\n");
  else
    printf ("FAIL (%d MB, %d areas, contents %s)\n", got * 16, areas,
	    ok ? "ok" : "changed");
  return 0;
}
