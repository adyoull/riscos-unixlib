/* Rename a file.
   Copyright (c) 2005-2014 UnixLib Developers.  */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <unixlib/local.h>
#include <internal/local.h>
#include <internal/os.h>
#include <internal/unix.h>
#include <internal/swiparams.h>
#include <pthread.h>

int
rename (const char *old_name, const char *new_name)
{
  PTHREAD_UNSAFE

  if (old_name == NULL || new_name == NULL)
    return __set_errno (EFAULT);

  if (old_name[0] == '\0' || new_name[0] == '\0')
    return __set_errno (ENOENT);

  /* oftype and nftype are the types the names imply, including a type
     MimeMap guesses from an extension: they decide the new name's type at
     the end.  oxtype and nxtype are only an explicit ",xyz", as stat()
     and open() check (get_attrs in common/objattr.c): a name's type is
     only a reason to say a file doesn't exist, or isn't a directory, when
     the name states it.  A Text "folderlist.xml" exists, although MimeMap
     maps ".xml" to &F80 (riscos-gtk, 2026).  As in get_attrs, only a
     file's type is checked; an image file is one when the imagefs
     feature says so (SWI_OS_File_ReadCatInfo already reports it as 1 or
     2, except in the SCL build).  */
  int oftype, oxtype;
  char ofile[MAXPATHLEN];
  if (!__riscosify (old_name, 0,
		    __get_riscosify_control () | __RISCOSIFY_FILETYPE_NOT_SET,
		    ofile, sizeof (ofile), &oxtype)
      || !__riscosify_std (old_name, 0, ofile, sizeof (ofile), &oftype))
    return __set_errno (ENAMETOOLONG);
  /* Fetch real objtype and filetype of old_name.  */
  unsigned otype, oloadaddr;
  if (SWI_OS_File_ReadCatInfo (ofile, &otype, &oloadaddr, NULL, NULL, NULL) != NULL)
    otype = 0;
  unsigned oftype_ro;
  if ((oloadaddr & 0xfff00000U) == 0xfff00000U)
    oftype_ro = (oloadaddr >> 8) & 0xfff;
  else
    oftype_ro = __RISCOSIFY_FILETYPE_NOTFOUND;

  /* Let __riscosify_std () create the directory if necessary. This is so
     rename ("foo", "foo.c") works when "c" does not exist.  */
  int nftype, nxtype;
  char nfile[MAXPATHLEN];
  if (!__riscosify (new_name, 0,
		    __get_riscosify_control () | __RISCOSIFY_FILETYPE_NOT_SET,
		    nfile, sizeof (nfile), &nxtype)
      || !__riscosify_std (new_name, 1, nfile, sizeof (nfile), &nftype))
    return __set_errno (ENAMETOOLONG);
  int ntype = __object_exists_ro (nfile);

  /* Does the old file/directory exist ? If so, and the new object exists
     as well, is it the same type ? */
  if (otype == 0
      || ((otype == 1 || (otype == 3 && __get_feature_imagefs_is_file ()))
	  && oxtype != __RISCOSIFY_FILETYPE_NOTFOUND
	  && (unsigned) oxtype != oftype_ro))
    {
      /* FIXME: if one of the directories in ofile does not exist, return
	 ENOTDIR instead.  */
      return __set_errno (ENOENT);
    }
  else if (otype == 1 && ntype == 2)
    return __set_errno (EISDIR);
  else if (otype == 2
	   && (ntype == 1 || nxtype != __RISCOSIFY_FILETYPE_NOTFOUND))
    return __set_errno (ENOTDIR);

  /* Do a RISC OS rename.  If that works ok, we're done.  This will
     deal with ofile and nfile being the same or only being different
     case-wise for both case-sensitive and case-insensitive filing systems.  */
  const _kernel_oserror *err;
  if ((err = SWI_OS_FSControl_Rename (ofile, nfile)) != NULL)
    {
      switch (mask_fs_num (err->errnum))
	{
	  case 0x100D6:
	    /* A RISC OS FS error 0x1xxD6 "Not found" indicates not all
	       directories of nfile exist.  */
	    return __set_errno (ENOENT);
	  case 0x100B0:
	    /* A RISC OS FS error 0x1xxB0 "Bad RENAME" indicates we're moving
	       a directory into in its subdirectory.  FIXME: check on otype == 2 needed ?  */
	    return __set_errno (EINVAL);
	}

      if (ntype == 0)
	return __ul_seterr (err, EOPSYS); /* FIXME: ok ? */

      /* Straight rename failed, so delete nfile (now we're sure ofile and
	 nfile aren't the same file/directory) and retry.  */
      if (SWI_OS_File_DeleteObject (nfile) != NULL)
	return __set_errno (ntype == 2 ? ENOTEMPTY : EPERM); /* FIXME: could be EROFS ? */

      /* Perform the rename again.  */
      if ((err = SWI_OS_FSControl_Rename (ofile, nfile)) != NULL)
	{
	  /* We check on RISC OS error 0xB0 "Bad RENAME" which probably is
	     caused by a rename across file systems.  */
	  if (err->errnum == 0xB0)
	    return __set_errno (EXDEV);
	  if (mask_fs_num (err->errnum) == 0x100B0)
	    return __set_errno (EINVAL);
	  return __ul_seterr (err, EOPSYS); /* FIXME: ok ? */
	}
    }

  /* The file's type follows its name when its name says nothing about the
     type, or when the type it has is the one its old name gave it: then
     it gets the new name's type if that is different.  A file whose type
     differs from what its name implies (a Text "folderlist.xml", or an
     untyped "x.xml" with load and execution addresses) keeps its type,
     unless the new name states one with ",xyz".  (An untyped "prog"
     renamed "prog.xml" gets &F80, as before.)  Never on a directory.  */
  int oftype_a = (oftype == __RISCOSIFY_FILETYPE_NOTFOUND) ? 0xfff : oftype;
  int nftype_a = (nftype == __RISCOSIFY_FILETYPE_NOTFOUND) ? 0xfff : nftype;
  int settype = -1;
  if (otype == 2)
    ;
  else if (oftype == __RISCOSIFY_FILETYPE_NOTFOUND
	   || (unsigned) oftype == oftype_ro)
    {
      if (oftype_a != nftype_a)
	settype = nftype_a;
    }
  else if (nxtype != __RISCOSIFY_FILETYPE_NOTFOUND
	   && (unsigned) nxtype != oftype_ro)
    settype = nxtype;
  if (settype != -1)
    {
      /* We need to set the filetype of the file.  */
      if ((err = SWI_OS_File_WriteCatInfoFileType (nfile, settype)) != NULL)
	return __ul_seterr (err, EOPSYS);
    }

  /* Delete the suffix swap dir if it is now empty */
  __unlinksuffix (ofile); /* ofile is corrupted by this call */

  return 0;
}
