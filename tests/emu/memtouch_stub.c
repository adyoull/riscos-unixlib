/* Linked by memtouch_test.py: it calls memcpy, memmove, memset, bcopy,
   mempcpy and setjmp directly, so this only makes the linker keep them.  */
#define _GNU_SOURCE
#include <string.h>
#include <strings.h>
#include <setjmp.h>

void *volatile keep[] = { memcpy, memmove, memset, bcopy, mempcpy, setjmp };

void
t_main (void)
{
}
