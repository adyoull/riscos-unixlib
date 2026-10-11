/* Host test for common/prefix.c: __get_dde_prefix measures the prefix up
   to its control-character terminator.  It used to test the first
   character over and over, so with a prefix set it never returned.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "internal/os.h"

const char *__get_dde_prefix (void);

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const char *fake_prefix;
static int fake_known = 1;
const _kernel_oserror *SWI_DDEUtils_ReadPrefix (int task, const char **p)
{
  static _kernel_oserror e = { 1, "No such SWI" };
  (void) task;
  if (!fake_known)
    return &e;
  *p = fake_prefix;
  return NULL;
}
char *__getenv_from_os (const char *n, char *b, int l, char **c)
{
  (void) n; (void) b; (void) l; (void) c;
  return strdup ("SDFS::Pi.$.Work");
}

static void timeout (int sig) { (void) sig; printf ("FAIL: hung\n"); _exit (1); }

int main (void)
{
  const char *r;
  signal (SIGALRM, timeout);
  alarm (5);

  fake_prefix = "ADFS::HardDisc4.$.Src\r";	/* control-char terminated */
  r = __get_dde_prefix ();
  CHECK (r && strcmp (r, "ADFS::HardDisc4.$.Src") == 0);
  free ((void *) r);

  fake_prefix = "\r";				/* empty */
  CHECK (__get_dde_prefix () == NULL);
  fake_prefix = NULL;				/* none set */
  CHECK (__get_dde_prefix () == NULL);

  fake_known = 0;				/* old DDEUtils: Prefix$Dir */
  r = __get_dde_prefix ();
  CHECK (r && strcmp (r, "SDFS::Pi.$.Work") == 0);
  free ((void *) r);

  printf ("prefix: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
