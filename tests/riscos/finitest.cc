// finitest: the program's .fini_array runs at exit (riscos-unixlib,
// 5.0.3.4-rc1; Rob Heaton's riscos-gcc16 report 22).
//
// Before, nothing called the .fini_array: a C function marked
// __attribute__ ((destructor)) never ran.  Now, as in glibc, the exit order
// is: functions registered with atexit () (last first), then static C++
// destructors (registered as the constructors ran), then the .fini_array
// (last entry first).  A C++ exception thrown and caught on the way must
// still work.  The last destructor prints PASS or FAIL.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

static char order[16];
static int n;

static void note (char c)
{
  if (n < (int) sizeof order - 1)
    order[n++] = c;
}

struct Static
{
  ~Static () { note ('S'); }
};
static Static object;

extern "C" __attribute__ ((destructor)) void fini_first (void)
{
  note ('F');
}

// Lower priority numbers run later as destructors: this one runs last.
extern "C" __attribute__ ((destructor (101))) void fini_last (void)
{
  note ('L');
  order[n] = 0;
  std::printf ("exit order %s (want ASFL): %s\n", order,
	       std::strcmp (order, "ASFL") == 0 ? "PASS" : "FAIL");
}

static void at_exit (void)
{
  note ('A');
}

int main (void)
{
  std::atexit (at_exit);
  try
    {
      throw std::runtime_error ("caught");
    }
  catch (const std::exception &e)
    {
      std::printf ("exception %s; exiting\n", e.what ());
    }
  return 0;
}
