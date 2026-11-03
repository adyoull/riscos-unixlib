/* Host test shim for stdio/rename.c (with common/riscosify.c), included
   first. The SWIs are faked by test_rename.c: a small table of files and
   directories, and a MimeMap that knows .xml, .html and .txt.  */
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#undef __nonnull
#define __nonnull(x)
#undef __wur
#define __wur
#undef __THROW
#define __THROW
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
typedef struct { int r[10]; } _kernel_swi_regs;
_kernel_oserror *_kernel_swi (int, _kernel_swi_regs *, _kernel_swi_regs *);
_kernel_oserror *__os_swi (int, int *);
#include <sys/param.h>			/* MAXPATHLEN */
extern const char __filename_char_map[256];
#define MMM_TYPE_RISCOS 0
#define MMM_TYPE_DOT_EXTN 3
_kernel_oserror *SWI_OS_File_ReadCatInfo (const char *, unsigned *, unsigned *,
					  unsigned *, unsigned *, unsigned *);
_kernel_oserror *SWI_OS_FSControl_Rename (const char *, const char *);
_kernel_oserror *SWI_OS_File_WriteCatInfoFileType (const char *, unsigned);
_kernel_oserror *SWI_OS_File_DeleteObject (const char *);
int __object_exists_ro (const char *);
void __unlinksuffix (char *);
void __sdirinit (void);
int __get_feature_imagefs_is_file (void);
int __ul_seterr (const _kernel_oserror *, int);
#define __set_errno(val) (errno = (val), -1)
#define mask_fs_num(errnum) ((errnum) & 0xFFFF00FF)
#define EOPSYS 88
#define PTHREAD_UNSAFE
#define stricmp strcasecmp
#include <strings.h>
