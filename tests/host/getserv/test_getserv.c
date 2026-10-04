/* Host test for netlib/getserv_r.c: the copy into the caller's buffer.
   getservbyname/getservbyport/getservent are faked.  */
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL: " __VA_ARGS__); printf ("\n"); } } while (0)

static char *aliases[] = { "www", "web", NULL };
static struct servent http = { "http", aliases, 0x5000, "tcp" };
static char *noal[] = { NULL };
static struct servent ssh = { "ssh", noal, 0x1600, "tcp" };

struct servent *getservbyname (const char *n, const char *p)
{ (void) p; return !strcmp (n, "http") ? &http : !strcmp (n, "ssh") ? &ssh : NULL; }
struct servent *getservbyport (int port, const char *p)
{ (void) p; return port == 0x5000 ? &http : NULL; }
struct servent *getservent (void) { return &ssh; }
unsigned int __servent_generation;

int main (void)
{
  struct servent se, *r = (void *) 1;
  char buf[256], small[16];
  int e;

  e = getservbyname_r ("http", "tcp", &se, buf, sizeof buf, &r);
  CHECK (e == 0 && r == &se, "http found");
  CHECK (!strcmp (se.s_name, "http") && !strcmp (se.s_proto, "tcp")
	 && se.s_port == 0x5000, "fields");
  CHECK (se.s_aliases && !strcmp (se.s_aliases[0], "www")
	 && !strcmp (se.s_aliases[1], "web") && !se.s_aliases[2], "aliases");
  CHECK ((char *) se.s_aliases >= buf && (char *) se.s_aliases < buf + sizeof buf
	 && se.s_name >= buf && se.s_name < buf + sizeof buf, "copied into buf");
  CHECK (((uintptr_t) se.s_aliases & (sizeof (char *) - 1)) == 0, "aligned");
  aliases[0] = "changed";
  CHECK (!strcmp (se.s_aliases[0], "www"), "independent of the static result");
  aliases[0] = "www";

  /* Odd buffer address: still aligned, still fits.  */
  e = getservbyname_r ("http", "tcp", &se, buf + 1, sizeof buf - 1, &r);
  CHECK (e == 0 && r == &se && ((uintptr_t) se.s_aliases & 3) == 0, "odd buf");

  r = (void *) 1;
  e = getservbyname_r ("nope", "tcp", &se, buf, sizeof buf, &r);
  CHECK (e == 0 && r == NULL, "not found: 0, NULL");

  r = (void *) 1;
  e = getservbyname_r ("http", "tcp", &se, small, sizeof small, &r);
  CHECK (e == ERANGE && r == NULL, "too small: ERANGE");

  /* Exactly enough: 3 pointers + "http" "tcp" "www" "web" = 3*P + 17.  */
  {
    static union { char c[64]; void *p; } u;
    size_t need = 3 * sizeof (char *) + 17;
    e = getservbyname_r ("http", "tcp", &se, u.c, need, &r);
    CHECK (e == 0 && r == &se, "exact size fits");
    e = getservbyname_r ("http", "tcp", &se, u.c, need - 1, &r);
    CHECK (e == ERANGE, "one byte short");
  }

  e = getservbyport_r (0x5000, NULL, &se, buf, sizeof buf, &r);
  CHECK (e == 0 && r && !strcmp (se.s_name, "http"), "by port");
  e = getservent_r (&se, buf, sizeof buf, &r);
  CHECK (e == 0 && r && !strcmp (se.s_name, "ssh") && se.s_aliases[0] == NULL,
	 "getservent_r, no aliases");

  printf ("getserv: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
