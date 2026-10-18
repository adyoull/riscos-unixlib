/* Host test shim for common/riscosify.c, included first. No RISC OS: SWIs
   fail, so there are no UnixFS$ special directories, no MimeMap and no
   files on disc; the suffix list is the default one.  */
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#define __nonnull(x)
#define __wur
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
#define stricmp strcasecmp
#include <strings.h>
