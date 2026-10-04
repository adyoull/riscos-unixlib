/* Host test for the services database (2026-10-04 audit): the reentrant
   lookups leave the program's strtok alone; getservent () walks the file
   and stops; getservent_r gives ENOENT at the end and the same entry
   again after ERANGE; lines without a name, port and protocol are
   skipped.  The file is out/InetDBase:Services (run.sh writes it).  */
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main (void)
{
  struct servent se, *r;
  char buf[256], small[8];
  int n, rc;

  /* A strtok loop that calls getservbyname_r keeps its own state.  */
  char list[] = "http,ssh,ntp";
  const char *want[] = { "http", "ssh", "ntp" };
  n = 0;
  for (char *t = strtok (list, ","); t; t = strtok (NULL, ","))
    {
      CHECK (n < 3 && strcmp (t, want[n]) == 0);
      rc = getservbyname_r (t, NULL, &se, buf, sizeof buf, &r);
      CHECK (rc == 0 && r == &se && strcmp (se.s_name, t) == 0);
      n++;
    }
  CHECK (n == 3);

  CHECK (getservbyname_r ("www", "tcp", &se, buf, sizeof buf, &r) == 0
	 && r && ntohs (se.s_port) == 80 && strcmp (se.s_aliases[1], "www-http") == 0);
  CHECK (getservbyport_r (htons (123), "udp", &se, buf, sizeof buf, &r) == 0
	 && r && strcmp (se.s_name, "ntp") == 0);

  /* getservent_r walks the file (blank and broken lines skipped), then
     ENOENT.  getservent () used to close the file after every call, so
     it returned the first entry for ever.  */
  setservent (0);
  const char *all[] = { "http", "ssh", "ntp" };
  n = 0;
  while ((rc = getservent_r (&se, buf, sizeof buf, &r)) == 0)
    {
      CHECK (n < 3 && r == &se && strcmp (se.s_name, all[n]) == 0);
      if (++n > 5)
	break;
    }
  CHECK (n == 3 && rc == ENOENT && r == NULL);
  endservent ();

  /* ERANGE, then the same entry with a big enough buffer.  */
  setservent (0);
  CHECK (getservent_r (&se, small, sizeof small, &r) == ERANGE && r == NULL);
  CHECK (getservent_r (&se, buf, sizeof buf, &r) == 0 && r
	 && strcmp (se.s_name, "http") == 0);
  CHECK (getservent_r (&se, buf, sizeof buf, &r) == 0 && r
	 && strcmp (se.s_name, "ssh") == 0);
  /* ... but not after the file has been read in between.  */
  CHECK (getservent_r (&se, small, sizeof small, &r) == ERANGE);
  CHECK (getservbyname_r ("http", NULL, &se, buf, sizeof buf, &r) == 0);
  setservent (0);
  CHECK (getservent_r (&se, buf, sizeof buf, &r) == 0 && r
	 && strcmp (se.s_name, "http") == 0);
  endservent ();

  /* getservent () itself: the walk ends.  */
  n = 0;
  while (getservent () != NULL && n < 10)
    n++;
  CHECK (n == 3);
  endservent ();

  printf ("services: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
