/* Host test shim: the host's grp.h plus UnixLib's internal reader.  */
#include_next <grp.h>
#include <stdio.h>
extern struct group *__grpread (FILE *, struct group *, char *, size_t);
