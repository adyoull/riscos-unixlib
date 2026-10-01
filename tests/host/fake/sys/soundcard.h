/* Use UnixLib's <sys/soundcard.h>, not the PC's: the request numbers must
   be RISC OS's (the PC's Linux header encodes them differently).  */
#ifdef _IOWR
#error "<sys/ioctl.h> came first: the encoding would not be soundcard.h's own"
#endif
#include "../../../../libunixlib/include/sys/soundcard.h"
