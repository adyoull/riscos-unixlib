/* In strict ISO C (-std=c99) nothing new is declared, so a program may
   still use these names for its own static functions.  */
#include <string.h>

static size_t
strnlen (const char *s, size_t n)
{
  size_t i = 0;
  while (i < n && s[i])
    i++;
  return i;
}

int
main (void)
{
  return (int) strnlen ("abc", 2) - 2;
}
