/* Host test for stdio/rename.c: a file's type and the type its name
   implies (2026, riscos-gtk). Built with the real common/riscosify.c over
   a fake filing system (a table of objects) and a fake MimeMap that maps
   .xml to &F80, .html to &FAF and .txt to &FFF.

   rename() said ENOENT for a Text "folderlist.xml" because MimeMap maps
   ".xml" to &F80 (Claws Mail couldn't save its folder list). Only a type
   the name states (",xyz", with __RISCOSIFY_FILETYPE_EXT) may make a file
   "not exist", as with stat() and open(). A file's type still follows its
   name as before when the type came from the name; one with a type of its
   own keeps it unless the new name states one.  */
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unixlib/local.h>

static _kernel_oserror notfound = { 0x100D6, "Not found" };
static _kernel_oserror other = { 1, "no such SWI here" };

/* The fake disc: objtype 1 = file, 2 = directory.  */
static struct { char name[64]; unsigned objtype, load; } obj[16];
static int nobj, type_writes, control;

static int
find (const char *n)
{
  for (int i = 0; i < nobj; i++)
    if (obj[i].objtype && strcasecmp (obj[i].name, n) == 0)
      return i;
  return -1;
}

/* TYPE is a filetype, or -1 for an untyped file (load address &8000).
   Directories have a typed, timestamped load address (&FFD here), as on a
   real disc.  */
static void
add (const char *n, unsigned objtype, int type)
{
  strcpy (obj[nobj].name, n);
  obj[nobj].objtype = objtype;
  obj[nobj++].load = type < 0 ? 0x8000 : 0xfff00000U | ((unsigned) type << 8);
}

static int
type_of (int i)
{
  return (obj[i].load & 0xfff00000U) == 0xfff00000U
    ? (int) ((obj[i].load >> 8) & 0xfff) : -2;
}

_kernel_oserror *
SWI_OS_File_ReadCatInfo (const char *f, unsigned *objtype, unsigned *load,
			 unsigned *exec, unsigned *len, unsigned *attr)
{
  int i = find (f);
  if (objtype)
    *objtype = i < 0 ? 0 : obj[i].objtype;
  if (load)
    *load = i < 0 ? 0 : obj[i].load;
  (void) exec; (void) len; (void) attr;
  return NULL;
}

int
__object_exists_ro (const char *f)
{
  int i = find (f);
  return i < 0 ? 0 : obj[i].objtype;
}

_kernel_oserror *
SWI_OS_FSControl_Rename (const char *a, const char *b)
{
  int i = find (a);
  if (i < 0)
    return &notfound;
  if (find (b) >= 0 && strcasecmp (a, b) != 0)
    return &other;			/* RISC OS won't overwrite */
  strcpy (obj[i].name, b);
  return NULL;
}

_kernel_oserror *
SWI_OS_File_WriteCatInfoFileType (const char *f, unsigned type)
{
  int i = find (f);
  type_writes++;
  if (i < 0)
    return &notfound;
  obj[i].load = 0xfff00000U | (type << 8);
  return NULL;
}

_kernel_oserror *
SWI_OS_File_DeleteObject (const char *f)
{
  int i = find (f);
  if (i >= 0)
    obj[i].objtype = 0;
  return NULL;
}

void __unlinksuffix (char *f) { (void) f; }
int __get_feature_imagefs_is_file (void) { return 1; }
int __ul_seterr (const _kernel_oserror *e, int n) { (void) e; errno = n; return -1; }

/* MimeMap_Translate (dot extension to RISC OS type) is the only SWI
   riscosify.c needs to succeed; the others fail (no UnixFS$ names).  */
_kernel_oserror *
__os_swi (int n, int *r)
{
  static const struct { const char *ext; int type; } map[] = {
    { ".xml", 0xF80 }, { ".html", 0xFAF }, { ".txt", 0xFFF } };
  if (n == 0x50b00 && r[0] == MMM_TYPE_DOT_EXTN)
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
      if (strcasecmp ((const char *) (unsigned long) (unsigned) r[1], map[i].ext) == 0)
	{
	  r[3] = map[i].type;
	  return NULL;
	}
  return &other;
}

_kernel_oserror *
_kernel_swi (int n, _kernel_swi_regs *i, _kernel_swi_regs *o)
{ (void) n; (void) i; (void) o; return &other; }
int __get_riscosify_control (void) { return control; }
struct passwd *__pwddefault (void)
{
  static struct passwd p = { "user", "", 0, 0, "", "/home", "" };
  return &p;
}

static int checks, failures;

/* rename OLD to NEW on a disc holding the objects set up by the caller;
   want WANT_ERR (0 = success), then NAME with TYPE (if NAME; -1: any, -2:
   untyped, load address &8000 kept).  */
static void
t (const char *what, const char *old, const char *new, int want_err,
   const char *name, int type)
{
  errno = 0;
  int r = rename (old, new);
  int err = r ? errno : 0;
  int i = name ? find (name) : -1;
  checks++;
  int bad = name && (i < 0
		     || (type >= 0 && type_of (i) != type)
		     || (type == -2 && obj[i].load != 0x8000));
  if (err != want_err || bad)
    {
      failures++;
      printf ("FAIL: %s: rename %s -> %s gave %s", what, old, new,
	      err ? strerror (err) : "ok");
      if (name)
	printf ("; %s %s &%X", name, i < 0 ? "missing" : "load",
		i < 0 ? 0 : obj[i].load);
      printf (" (want %s)\n", want_err ? strerror (want_err) : "ok");
    }
}

static void
reset (int c)
{
  nobj = 0;
  type_writes = 0;
  control = c;
}

static void
no_type_writes (const char *what)
{
  checks++;
  if (type_writes)
    {
      failures++;
      printf ("FAIL: %s: a filetype was written\n", what);
    }
}

int
main (void)
{
  __sfixinit ("c:cc:f:h:i:ii:l:o:p:s:y");	/* as unix/features.c */
  __sdirinit ();
  static const int ctls[] = {
    0, __RISCOSIFY_FILETYPE_EXT,
    __RISCOSIFY_FILETYPE_EXT | __RISCOSIFY_FILETYPE_FFF_EXT };
  for (int c = 0; c < 3; c++)
    {
      int ctl = ctls[c];

      /* The Claws Mail case: a Text file named .xml.  */
      reset (ctl);
      add ("folderlist/xml", 1, 0xFFF);
      t ("Text .xml to .xml.bak", "folderlist.xml", "folderlist.xml.bak", 0,
	 "folderlist/xml/bak", 0xFFF);

      /* A &F80 .xml, as UnixLib makes it: unchanged.  */
      reset (ctl);
      add ("a/xml", 1, 0xF80);
      t ("XML .xml to .bak", "a.xml", "a.bak", 0, "a/bak", 0xFFF);

      /* Temporary file into place: still typed from the new name.  */
      reset (ctl);
      add ("page/tmp", 1, 0xFFF);
      t ("tmp to .html", "page.tmp", "page.html", 0, "page/html", 0xFAF);

      /* A file whose type isn't its name's keeps it (these were ENOENT).  */
      reset (ctl);
      add ("b/html", 1, 0xFFF);
      t ("Text .html to .xml", "b.html", "b.xml", 0, "b/xml", 0xFFF);
      reset (ctl);
      add ("d/xml", 1, 0xFFD);
      t ("Data .xml to .xml.bak", "d.xml", "d.xml.bak", 0, "d/xml/bak", 0xFFD);
      reset (ctl);
      add ("x/xml", 1, -1);
      t ("untyped .xml to .bak", "x.xml", "x.xml.bak", 0, "x/xml/bak", -2);
      no_type_writes ("untyped .xml to .bak");

      /* Untyped, no extension, to .html: typed, as before.  */
      reset (ctl);
      add ("prog", 1, -1);
      t ("untyped to .html", "prog", "prog.html", 0, "prog/html", 0xFAF);

      /* Over an existing file (delete and retry).  */
      reset (ctl);
      add ("new/xml", 1, 0xF80);
      add ("old/xml", 1, 0xF80);
      t ("over an existing file", "old.xml", "new.xml", 0, "new/xml", 0xF80);
      checks++;
      if (find ("old/xml") >= 0)
	{
	  failures++;
	  printf ("FAIL: old/xml still there\n");
	}

      /* Suffix swapping: c.foo to c.bar.  */
      reset (ctl);
      add ("c.foo", 1, 0xFFF);
      t ("suffix swap", "foo.c", "bar.c", 0, "c.bar", 0xFFF);

      /* A missing file is still ENOENT.  */
      reset (ctl);
      t ("missing", "nothere.xml", "x.xml", ENOENT, NULL, -1);

      /* A directory whose name MimeMap knows, renamed: allowed (it was
	 ENOENT), and no filetype is written to it.  */
      reset (ctl);
      add ("site/html", 2, 0xFFD);
      t ("directory .html to plain", "site.html", "site2", 0, "site2", 0xFFD);
      no_type_writes ("directory .html to plain");

      /* A directory renamed to a name MimeMap knows: allowed, and no
	 filetype is written to it.  */
      reset (ctl);
      add ("dir", 2, 0xFFD);
      t ("directory to .html", "dir", "dir.html", 0, "dir/html", 0xFFD);
      no_type_writes ("directory to .html");
    }

  /* With __RISCOSIFY_FILETYPE_EXT, a stated ",xyz" type still has to
     match, as with stat() and open().  */
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("data", 1, 0xFFD);
  t (",ffd matches", "data,ffd", "data2,ffd", 0, "data2", 0xFFD);
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("data", 1, 0xFFD);
  t (",ff9 doesn't match", "data,ff9", "data2", ENOENT, "data", 0xFFD);
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("dir", 2, 0xFFD);
  t ("directory to ,ffd", "dir", "file,ffd", ENOTDIR, "dir", -1);

  /* A stated new type is set, even on a file with a type of its own.  */
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("note/xml", 1, 0xFFF);
  t ("Text .xml to ,ffd", "note.xml", "note,ffd", 0, "note", 0xFFD);

  /* ,fff counts only with __RISCOSIFY_FILETYPE_FFF_EXT.  */
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("t", 1, 0xFFD);
  t (",fff without FFF_EXT", "t,fff", "t2", 0, "t2", 0xFFD);
  reset (__RISCOSIFY_FILETYPE_EXT | __RISCOSIFY_FILETYPE_FFF_EXT);
  add ("t", 1, 0xFFD);
  t (",fff with FFF_EXT", "t,fff", "t2", ENOENT, "t", 0xFFD);

  /* Directories aren't checked against a stated type, as with stat().  */
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("dir", 2, 0xFFD);
  t ("directory named ,ffd", "dir,ffd", "dir2", 0, "dir2", -1);

  /* An image file (objtype 3; the real SWI_OS_File_ReadCatInfo reports it
     as a file when the imagefs feature says so) is checked like a file.  */
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("arc", 3, 0xDDC);
  t ("image file, wrong ,xyz", "arc,ffd", "x", ENOENT, "arc", 0xDDC);
  reset (__RISCOSIFY_FILETYPE_EXT);
  add ("arc", 3, 0xDDC);
  t ("image file, right ,xyz", "arc,ddc", "arc2,ddc", 0, "arc2", 0xDDC);

  /* The program's own __RISCOSIFY_FILETYPE_NOT_SET: no MimeMap at all, so
     a Text .xml renames, and .html gives no type.  */
  reset (__RISCOSIFY_FILETYPE_NOT_SET);
  add ("e/xml", 1, 0xFFF);
  t ("NOT_SET Text .xml", "e.xml", "e.xml.bak", 0, "e/xml/bak", 0xFFF);
  reset (__RISCOSIFY_FILETYPE_NOT_SET);
  add ("note2/tmp", 1, 0xFFF);
  t ("NOT_SET tmp to .html", "note2.tmp", "note2.html", 0, "note2/html", 0xFFF);

  /* __RISCOSIFY_NO_PROCESS: names are RISC OS names, unchanged.  */
  reset (__RISCOSIFY_NO_PROCESS);
  add ("g/xml", 1, 0xFFF);
  t ("NO_PROCESS", "g/xml", "g/bak", 0, "g/bak", 0xFFF);

  printf ("rename: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
