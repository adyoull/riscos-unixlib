/* Host test for grp/getgrgid_r.c, grp/getgrnam_r.c, pwd/getpwuid_r.c and
   pwd/getpwnam_r.c (2026): *result is NULL unless an entry is returned;
   0 for "no such entry", an error number otherwise (ERANGE when a line
   doesn't fit the buffer); getpwnam_r keeps the name it found. Before,
   they returned -1 and left *result unset, and getpwnam_r overwrote
   pw_name with pw_passwd.  */
#undef fopen
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>

static const char *group_file, *passwd_file;

FILE *
test_fopen (const char *p, const char *m)
{
  const char *f = strcmp (p, "/etc/group") == 0 ? group_file
		  : strcmp (p, "/etc/passwd") == 0 ? passwd_file : p;
  if (f == NULL)
    {
      errno = ENOENT;
      return NULL;
    }
  return fopen (f, m);
}

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void *junk = (void *) 0x5A5A5A5A;

int
main (void)
{
  char buf[256], tiny[12];
  struct group g, *gr;
  struct passwd p, *pr;
  FILE *f;
  int r;

  /* No /etc/group (always so on RISC OS): no entries.  */
  group_file = NULL;
  gr = junk;
  r = getgrgid_r (0, &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == NULL);
  gr = junk;
  r = getgrnam_r ("wheel", &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == NULL);

  group_file = "out/group";
  f = fopen ("out/group", "w");
  fputs ("# comment\nwheel:x:0:root\nstaff:x:50:alice,bob\n", f);
  fclose (f);
  gr = junk;
  r = getgrgid_r (50, &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == &g && strcmp (g.gr_name, "staff") == 0);
  gr = junk;
  r = getgrgid_r (99, &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == NULL);
  gr = junk;
  r = getgrnam_r ("wheel", &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == &g && g.gr_gid == 0);
  gr = junk;
  r = getgrnam_r ("nobody", &g, buf, sizeof buf, &gr);
  CHECK (r == 0 && gr == NULL);
  gr = junk;
  r = getgrgid_r (50, &g, tiny, sizeof tiny, &gr);	/* a line doesn't fit */
  CHECK (r == ERANGE && gr == NULL);
  gr = junk;
  r = getgrgid_r (0, NULL, buf, sizeof buf, &gr);
  CHECK (r == EINVAL && gr == NULL);

  /* No /etc/passwd: the default entry, in the caller's buffer.  */
  passwd_file = NULL;
  pr = junk;
  r = getpwuid_r (0, &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == &p && p.pw_name >= buf && p.pw_name < buf + sizeof buf);
  pr = junk;
  r = getpwuid_r (0, &p, tiny, 2, &pr);
  CHECK (r == ERANGE && pr == NULL);
  pr = junk;
  r = getpwnam_r ("anyone", &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == &p);

  passwd_file = "out/passwd";
  f = fopen ("out/passwd", "w");
  fputs ("root:secret:0:0:Root:/root:/bin/sh\nalice:pw:1000:50:Alice:/home/alice:/bin/sh\n", f);
  fclose (f);
  pr = junk;
  r = getpwuid_r (1000, &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == &p && strcmp (p.pw_name, "alice") == 0);
  pr = junk;
  r = getpwuid_r (4242, &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == NULL);
  pr = junk;
  r = getpwnam_r ("alice", &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == &p && strcmp (p.pw_name, "alice") == 0
	 && strcmp (p.pw_dir, "/home/alice") == 0);
  pr = junk;
  r = getpwnam_r ("mallory", &p, buf, sizeof buf, &pr);
  CHECK (r == 0 && pr == NULL);
  pr = junk;
  r = getpwnam_r ("alice", &p, tiny, sizeof tiny, &pr);
  CHECK (r == ERANGE && pr == NULL);

  printf ("getpwgr: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
