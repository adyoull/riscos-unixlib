/* The shared fake swis.h plus the SWI numbers pthread/ticker.c uses.  */
#include "../../fake/swis.h"
#define OS_File                 0x08
#define OS_Args                 0x09
#define OS_GBPB                 0x0c
#define OS_Find                 0x0d
#define OS_GetEnv               0x10
#define OS_SynchroniseCodeAreas 0x6e
#define Wimp_ReadSysInfo        0x400f2
#define Filter_RegisterPreFilter      0x42640
#define Filter_RegisterPostFilter     0x42641
#define Filter_DeRegisterPreFilter    0x42642
#define Filter_DeRegisterPostFilter   0x42643
