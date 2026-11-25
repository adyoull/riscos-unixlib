/* Written by Alex Waugh.
   Copyright (c) 2002, 2003, 2004, 2005, 2006 UnixLib Developers.  */

#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>

#include <internal/unix.h>

/* Separated from key.c to avoid other key functions being
   pulled in by the linker */

/* Call all the destructors of a thread specific key */
void
__pthread_key_calldestructors (void)
{
  struct __pthread_key *key;
  int tries = 0;

  /* 2026 (K13, found by riscos-unixlib-web W46): pthread_exit calls this
     with thread switching on, so that the destructors can block, but the
     key nodes were freed with free_unlocked as well: another thread in
     malloc or free at that moment could find the heap half-changed.
     The node is now taken off the list and freed with switching held off,
     as pthread_setspecific and pthread_key_delete do, and the destructor
     is called with switching on, as before.  */
  for (;;)
    {
      void *value = NULL;
      void (*destructor) (void *) = NULL;

      __pthread_disable_ints ();
      key = __pthread_running_thread->keys;
      if (key != NULL)
	{
	  __pthread_running_thread->keys = key->next;
	  value = key->value.nonconst;
	  destructor = key->destructor;
	  free_unlocked (__ul_global.malloc_state, key);
	}
      __pthread_enable_ints ();
      if (key == NULL)
	break;

      if (value != NULL && destructor != NULL
	  && tries < PTHREAD_DESTRUCTOR_ITERATIONS)
        {
          tries++;
          destructor (value);
        }
    }
}
