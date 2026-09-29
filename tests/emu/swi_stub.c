/* Entry points for tests/emu/swi_test.py: calls into the real library code
   (linked from build/work/build/.libs/libunixlib.a), run in the Unicorn
   ARM emulator with the SWIs faked.  Not a RISC OS program.  */
#include <stddef.h>
#include <internal/fd.h>

extern char *__standard_time (const char *riscos_time, char *local_buffer);
extern int __fsread (struct __unixlib_fd *file_desc, void *data, int nbyte);

static const char ro_time[5] = { 1, 2, 3, 4, 5 };

char *
t_standard_time (char *local_buffer)
{
  return __standard_time (ro_time, local_buffer);
}

int
t_fsread (void *buf, int nbyte)
{
  struct __unixlib_fd_handle handle = { 1, 0, (void *) 42 };
  struct __unixlib_fd fd = { &handle, 0, 0 };
  return __fsread (&fd, buf, nbyte);
}
