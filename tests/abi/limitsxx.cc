// <climits> in a C++ constant expression (harfbuzz's hb-meta.hh does this).
#include <climits>
template <class T, T v> struct ic { static const T value = v; };
static_assert (ic<signed long long, LLONG_MIN>::value < 0, "LLONG_MIN");
int limitsxx_ok;
