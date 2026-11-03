/* renametest: rename() of a file whose type isn't the one MimeMap gives
   its extension (riscos-unixlib M1, from riscos-gtk: Claws Mail couldn't
   save folderlist.xml when it was Text). Needs MimeMap (the desktop) to
   test anything; says so when MimeMap doesn't know .xml.  */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>

static int bad;

static int
type_of (const char *ro)
{
  int obj, load;
  if (_swix (OS_File, _INR(0,1) | _OUT(0) | _OUT(2), 17, ro, &obj, &load)
      || obj == 0)
    return -1;
  return (load & 0xfff00000) == 0xfff00000 ? (load >> 8) & 0xfff : -2;
}

/* Make UX typed TYPE, rename it to UX2, and want UX2 typed WANT.  */
static void
check (const char *what, const char *ux, int type, const char *ux2, int want)
{
  char ro[256], ro2[256];
  FILE *f = fopen (ux, "w");
  if (!f) { perror (ux); bad = 1; return; }
  fputs ("<test/>\n", f);
  fclose (f);
  __riscosify_std (ux, 0, ro, sizeof ro, NULL);
  __riscosify_std (ux2, 0, ro2, sizeof ro2, NULL);
  _swix (OS_File, _INR(0,2), 18, ro, type);
  remove (ux2);
  int r = rename (ux, ux2);
  int got = type_of (ro2);
  int ok = r == 0 && got == want;
  printf ("%-26s rename: %s, type &%03X (want &%03X): %s\n", what,
	  r ? strerror (errno) : "ok", got < 0 ? 0 : got, want,
	  ok ? "ok" : "FAIL");
  bad |= !ok;
  remove (ux2);
  remove (ux);
}

int
main (void)
{
  int t = -1;
  if (_swix (0x50b00 /* MimeMap_Translate */, _INR(0,2) | _OUT(3),
	     3, ".xml", 0, &t) || t == 0xfff)
    {
      printf ("MimeMap doesn't know .xml here (not in the desktop?): the old "
	      "rename() worked too, so this proves nothing\n");
      t = 0xfff;
    }
  else
    printf ("MimeMap: .xml is &%03X\n", t);
  check ("Text .xml to .xml.bak", "/<Wimp$ScrapDir>/rntest.xml", 0xfff,
	 "/<Wimp$ScrapDir>/rntest.xml.bak", 0xfff);
  check ("Data .xml to .bak", "/<Wimp$ScrapDir>/rntest.xml", 0xffd,
	 "/<Wimp$ScrapDir>/rntest.bak", 0xffd);
  check ("XML .xml to .bak", "/<Wimp$ScrapDir>/rntest.xml", t,
	 "/<Wimp$ScrapDir>/rntest.bak", 0xfff);
  check ("Text .tmp to .xml", "/<Wimp$ScrapDir>/rntest.tmp", 0xfff,
	 "/<Wimp$ScrapDir>/rntest.xml", t);
  printf ("%s\n", bad ? "FAIL" : "PASS");
  return bad;
}
