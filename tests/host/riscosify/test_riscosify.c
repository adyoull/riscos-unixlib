/* Host test for common/riscosify.c (__riscosify): Unix names to RISC OS
   names, with no RISC OS underneath (no UnixFS$ directories, no MimeMap,
   no files), so only the name rules are checked. The vectors are the
   examples in riscosify.c's own comments, plus (2026) names starting with
   a run of slashes, which mean one "/" (riscos-gtk: Claws Mail made
   "///RAM::RamDisc0.$/Textfile.txt" from a file:/// URI).  */
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unixlib/local.h>

_kernel_oserror err = { 1, "no RISC OS here" };
_kernel_oserror *_kernel_swi (int n, _kernel_swi_regs *i, _kernel_swi_regs *o)
{ (void) n; (void) i; (void) o; return &err; }
_kernel_oserror *__os_swi (int n, int *r) { (void) n; (void) r; return &err; }
_kernel_oserror *SWI_OS_File_ReadCatInfo (const char *f, unsigned *a, unsigned *b,
					  unsigned *c, unsigned *d, unsigned *e)
{ (void) f; (void) a; (void) b; (void) c; (void) d; (void) e; return &err; }
int __get_riscosify_control (void) { return 0; }
struct passwd *__pwddefault (void)
{
  static struct passwd p = { "user", "", 0, 0, "", "/home", "" };
  return &p;
}

static int checks, failures;

static void
t (const char *in, const char *want)
{
  char buf[256];
  const char *got = __riscosify (in, 0, 0, buf, sizeof buf, NULL) ? buf : "(NULL)";
  checks++;
  if (strcmp (got, want) != 0)
    {
      failures++;
      printf ("FAIL: %-36s -> %-28s want %s\n", in, got, want);
    }
}

int
main (void)
{
  __sfixinit ("c:cc:f:h:i:ii:l:o:p:s:y");	/* as unix/features.c */
  __sdirinit ();
  /* From riscosify.c's comments (behaviour before 2026, unchanged).  */
  t ("/idefs::hd/$/fred/preset/s", "idefs::hd.$.fred.preset.s");
  t ("/idefs::hd/$/fred/preset.s", "idefs::hd.$.fred.s.preset");
  t ("/HostFS:$/jo.html", "HostFS:$.jo/html");
  t ("/arm/rname.c", "$.arm.c.rname");
  t ("/<GCC$Dir>/config", "<GCC$Dir>.config");
  t ("/gcc:/libgcc.o", "gcc:o.libgcc");
  t ("/gcc:libgcc.o", "gcc:o.libgcc");
  t ("/dev/tty", "tty:");
  t ("RAM::RamDisc0.$.Textfile/txt", "RAM::RamDisc0.$.Textfile/txt");

  /* A run of leading slashes is one "/".  */
  t ("///RAM::RamDisc0.$/Textfile.txt", "RAM::RamDisc0.$.Textfile/txt");
  t ("//RAM::RamDisc0.$/Textfile.txt", "RAM::RamDisc0.$.Textfile/txt");
  t ("/RAM::RamDisc0.$/Textfile.txt", "RAM::RamDisc0.$.Textfile/txt");
  t ("////idefs::hd/$/fred/preset/s", "idefs::hd.$.fred.preset.s");
  t ("///HostFS:$/jo.html", "HostFS:$.jo/html");
  t ("///<GCC$Dir>/config", "<GCC$Dir>.config");
  t ("//gcc:/libgcc.o", "gcc:o.libgcc");
  t ("///arm/rname.c", "$.arm.c.rname");
  t ("///dev/tty", "tty:");

  printf ("riscosify: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
