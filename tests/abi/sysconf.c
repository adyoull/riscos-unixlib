/* <unistd.h>: every _SC_ name has a value of its own (2026, C2).
   _SC_PAGE_SIZE = _SC_PAGESIZE used to restart the numbering, so
   _SC_NPROCESSORS_ONLN equalled _SC_BC_DIM_MAX and sysconf gave 2048.
   Compiled by check.sh; the static assertions fail with the old header.  */
#include <unistd.h>

#ifndef _SC_NPROCESSORS_ONLN
#error "_SC_NPROCESSORS_ONLN isn't a macro: #ifdef tests of it fail"
#endif
#ifndef _SC_NPROCESSORS_CONF
#error "_SC_NPROCESSORS_CONF missing"
#endif
_Static_assert (_SC_PAGE_SIZE == _SC_PAGESIZE, "PAGE_SIZE is PAGESIZE");
_Static_assert (_SC_NPROCESSORS_ONLN > _SC_2_LOCALEDEF
		&& _SC_NPROCESSORS_CONF > _SC_2_LOCALEDEF
		&& _SC_PHYS_PAGES > _SC_2_LOCALEDEF,
		"the names after the alias don't reuse earlier values");
_Static_assert (_SC_NPROCESSORS_ONLN != _SC_BC_DIM_MAX, "ONLN isn't BC_DIM_MAX");
_Static_assert (_SC_PHYS_PAGES != _SC_BC_BASE_MAX, "PHYS_PAGES isn't BC_BASE_MAX");
