/* Dynamic area probe (riscos-unixlib): which way can a UnixLib heap get
   past 128 MB for a single block?  Changes nothing outside itself: every
   area it makes is removed before it ends.

   A. Adjacent areas: make a normal area, then ask for a second one with
      its base (R3) just after the first one's maximum.  If RISC OS puts it
      there, fill the first to its maximum and write across the join.
   B. Physical memory pool (PMP) areas, as ARMEABISupport makes for mmap:
      ask for maximums of 256 MB, 512 MB and 1 GB, then claim and map
      140 MB of pages (more than 128 MB) and check them, plus one page near
      the end.

   daprobe [logfile]   prints the results, and copies them to logfile.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <swis.h>

#define MB (1u << 20)
#define PAGE 4096u
#define FLAG_NOT_DRAGGABLE 0x80
#define FLAG_SPECIFIC_PAGES (1u << 8)
#define FLAG_PMP (1u << 20)
#define PMP_KERNEL_CHOICE (-2)
#define BATCH 256

static FILE *logf;

static void
say (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  if (logf)
    {
      va_start (ap, fmt);
      vfprintf (logf, fmt, ap);
      va_end (ap);
    }
}

static const char *
errtext (const _kernel_oserror *e)
{
  static char buf[300];
  if (!e)
    return "no error";
  snprintf (buf, sizeof buf, "error &%X \"%s\"", e->errnum, e->errmess);
  return buf;
}

/* A PMP area's handler, in case RISC OS insists on one: refuses every
   request (as ARMEABISupport's does).  Only this program's areas use it,
   and they are all removed before the program ends.  */
__attribute__ ((naked)) static void
refuse_handler (void)
{
  __asm__ volatile ("MOV	r0, #0\n\t"
		    "CMP	r0, #1<<31\n\t"	/* clear V */
		    "CMNVC	r0, #1<<31\n\t"	/* set V */
		    "ADR	r0, 1f\n\t"
		    "MOV	pc, lr\n"
		    "1:\t.word	0\n\t"
		    ".asciz	\"daprobe area handler\"\n\t"
		    ".align	2\n");
}

static const _kernel_oserror *
area_create (unsigned flags, unsigned max, int base, void *handler,
	     unsigned pages, const char *name, int *num, unsigned *got_base,
	     unsigned *got_max)
{
  *num = 0;
  *got_base = 0;
  *got_max = 0;
  return _swix (OS_DynamicArea,
		_INR(0,9) | _OUT(1) | _OUT(3) | _OUT(5),
		0, -1, 0, base, flags, max, handler, -1, name, pages,
		num, got_base, got_max);
}

static void
area_remove (int num)
{
  const _kernel_oserror *e;
  if (num && (e = _swix (OS_DynamicArea, _INR(0,1), 1, num)) != NULL)
    say ("  !! removing area %d: %s\n", num, errtext (e));
}

/* ---- A: adjacent areas ---- */
static void
probe_adjacent (void)
{
  const _kernel_oserror *e;
  int a = 0, b = 0, i;
  unsigned abase, amax, bbase, bmax, want;
  int changed;

  say ("\nA. Adjacent areas\n");
  e = area_create (FLAG_NOT_DRAGGABLE, 512 * MB, -1, 0, 0, "DAProbe A",
		   &a, &abase, &amax);
  if (e)
    {
      say ("  first area: %s\n", errtext (e));
      return;
    }
  say ("  first area %d: base &%08X, max %u K (asked 512 MB)\n", a, abase,
       amax / 1024);

  for (i = 0; i < 2; i++)
    {
      want = abase + amax + (i ? 128 * MB : 0);
      e = area_create (FLAG_NOT_DRAGGABLE, 512 * MB, (int) want, 0, 0,
		       "DAProbe B", &b, &bbase, &bmax);
      say ("  second area asked at &%08X%s: %s", want,
	   i ? " (128 MB further on)" : " (just after the first)",
	   e ? errtext (e) : "");
      if (!e)
	say ("made %d at &%08X, max %u K%s\n", b, bbase, bmax / 1024,
	     bbase == want ? " - WHERE ASKED" : " - elsewhere");
      else
	say ("\n");
      if (!e && bbase == abase + amax)
	break;
      area_remove (b);
      b = 0;
    }

  if (b && bbase == abase + amax)
    {
      /* Fill the first to its maximum, 1 MB of the second, and write
	 across the join.  */
      e = _swix (OS_ChangeDynamicArea, _INR(0,1) | _OUT(1), a, amax,
		 &changed);
      if (!e)
	e = _swix (OS_ChangeDynamicArea, _INR(0,1) | _OUT(1), b, MB,
		   &changed);
      if (e)
	say ("  growing them: %s\n", errtext (e));
      else
	{
	  unsigned char *p = (unsigned char *) (abase + amax - 64 * 1024);
	  size_t n = 128 * 1024, j;
	  int ok = 1;
	  memset (p, 0xA5, n);
	  for (j = 0; j < n; j++)
	    if (p[j] != 0xA5)
	      ok = 0;
	  say ("  128 KB written across the join: %s\n",
	       ok ? "OK - the two areas work as one block" : "DATA WRONG");
	}
    }
  else
    say ("  RESULT A: areas can't be placed next to each other\n");
  if (b && bbase == abase + amax)
    say ("  RESULT A: adjacent areas WORK\n");
  area_remove (b);
  area_remove (a);
}

/* ---- B: PMP areas ---- */
static int
pmp_map (int area, unsigned first, unsigned count)
{
  static int phy[BATCH * 3], log[BATCH * 3];
  const _kernel_oserror *e;
  unsigned done = 0, k, n;

  while (done < count)
    {
      n = count - done < BATCH ? count - done : BATCH;
      for (k = 0; k < n; k++)
	{
	  phy[k * 3 + 0] = (int) (first + done + k);	/* page index */
	  phy[k * 3 + 1] = PMP_KERNEL_CHOICE;
	  phy[k * 3 + 2] = 0;
	  log[k * 3 + 0] = (int) (first + done + k);	/* logical page */
	  log[k * 3 + 1] = (int) (first + done + k);	/* page index */
	  log[k * 3 + 2] = 0;				/* access: RW */
	}
      e = _swix (OS_DynamicArea, _INR(0,3), 21, area, phy, n);
      if (!e)
	e = _swix (OS_DynamicArea, _INR(0,3), 22, area, log, n);
      if (e)
	{
	  say ("  mapping pages %u-%u: %s\n", first + done,
	       first + done + n - 1, errtext (e));
	  return 0;
	}
      done += n;
    }
  return 1;
}

static void
pmp_unmap (int area, unsigned first, unsigned count)
{
  static int log[BATCH * 3], phy[BATCH * 3];
  unsigned done = 0, k, n;
  while (done < count)
    {
      n = count - done < BATCH ? count - done : BATCH;
      for (k = 0; k < n; k++)
	{
	  log[k * 3 + 0] = (int) (first + done + k);
	  log[k * 3 + 1] = -1;			/* unmap */
	  log[k * 3 + 2] = 0;
	  phy[k * 3 + 0] = (int) (first + done + k);
	  phy[k * 3 + 1] = -1;			/* release */
	  phy[k * 3 + 2] = 0;
	}
      _swix (OS_DynamicArea, _INR(0,3), 22, area, log, n);
      _swix (OS_DynamicArea, _INR(0,3), 21, area, phy, n);
      done += n;
    }
}

static void
probe_pmp (void)
{
  static const unsigned sizes[] = { 256, 512, 1024 };
  unsigned s, best = 0;

  say ("\nB. Physical memory pool areas\n");
  for (s = 0; s < 3; s++)
    {
      const _kernel_oserror *e;
      unsigned want = sizes[s] * MB, base, max, pages = want / PAGE;
      int a, h;
      void *handler = 0;

      for (h = 0; h < 2; h++)
	{
	  handler = h ? (void *) refuse_handler : 0;
	  e = area_create (FLAG_NOT_DRAGGABLE | FLAG_PMP | FLAG_SPECIFIC_PAGES,
			   want, -1, handler, pages, "DAProbe PMP",
			   &a, &base, &max);
	  if (!e)
	    break;
	  say ("  %u MB, %s handler: %s\n", sizes[s], h ? "with a" : "no",
	       errtext (e));
	}
      if (e)
	continue;
      say ("  %u MB (%s handler): area %d at &%08X, max %u K\n", sizes[s],
	   handler ? "with a" : "no", a, base, max / 1024);

      if (max >= want)
	{
	  /* 140 MB from the start, and one page near the end.  */
	  unsigned n = 140 * MB / PAGE, last = pages - 2;
	  if (pmp_map (a, 0, n) && pmp_map (a, last, 1))
	    {
	      unsigned char *p = (unsigned char *) base;
	      unsigned j;
	      int ok = 1;
	      for (j = 0; j < 140 * MB; j += PAGE)
		p[j] = p[j + PAGE - 1] = (unsigned char) (j >> 12);
	      p[last * PAGE] = 0x5A;
	      for (j = 0; j < 140 * MB; j += PAGE)
		if (p[j] != (unsigned char) (j >> 12)
		    || p[j + PAGE - 1] != (unsigned char) (j >> 12))
		  ok = 0;
	      if (p[last * PAGE] != 0x5A)
		ok = 0;
	      say ("  140 MB mapped and written, and a page at %u MB: %s\n",
		   last * PAGE / MB, ok ? "OK" : "DATA WRONG");
	      if (ok)
		best = sizes[s];
	    }
	  pmp_unmap (a, 0, n);
	  pmp_unmap (a, last, 1);
	}
      area_remove (a);
    }
  if (best)
    say ("  RESULT B: PMP areas WORK, at least %u MB in one area\n", best);
  else
    say ("  RESULT B: no PMP area past 128 MB worked\n");
}

int
main (int argc, char **argv)
{
  if (argc > 1)
    logf = fopen (argv[1], "w");
  say ("DAProbe: can one heap block be bigger than 128 MB?\n");
  probe_adjacent ();
  probe_pmp ();
  say ("\nDone. All test areas removed%s%s.\n",
       logf ? "; log in " : "", logf ? argv[1] : "");
  if (logf)
    fclose (logf);
  return 0;
}
