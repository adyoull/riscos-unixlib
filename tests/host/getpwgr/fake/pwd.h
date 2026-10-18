/* Host test shim: the host's pwd.h plus UnixLib's internal readers.  */
#include_next <pwd.h>
#include <stdio.h>
extern struct passwd *__pwdread (FILE *, struct passwd *, char *, size_t);
extern struct passwd *__pwddefault (void);
