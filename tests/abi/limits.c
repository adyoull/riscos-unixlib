/* <limits.h>: the long long limits have the right type and sign (2026:
   LLONG_MIN was 0x8000000000000000LL, an unsigned long long).  */
#include <limits.h>
_Static_assert (LLONG_MIN < 0, "LLONG_MIN is negative");
_Static_assert (__builtin_types_compatible_p (__typeof__ (LLONG_MIN), long long),
		"LLONG_MIN is a long long");
_Static_assert (LLONG_MIN == -LLONG_MAX - 1, "LLONG_MIN value");
_Static_assert (LONG_LONG_MIN == LLONG_MIN, "LONG_LONG_MIN");
_Static_assert (__builtin_types_compatible_p (__typeof__ (LLONG_MAX), long long),
		"LLONG_MAX is a long long");
_Static_assert (__builtin_types_compatible_p (__typeof__ (ULLONG_MAX),
					      unsigned long long), "ULLONG_MAX");
int limits_ok;
