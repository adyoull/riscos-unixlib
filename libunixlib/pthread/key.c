/* Thread specific keys.
   Written by Alex Waugh.
   Copyright (c) 2002-2010 UnixLib Developers.  */

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <malloc.h>

#include <internal/unix.h>

/* ID to allocate to next new key.  */
static pthread_key_t nextkey = 0;

/* Linked list of all keys.  */
static struct __pthread_key *keys = NULL;

/* Create a key and associate a destructor function with it.  */
int
pthread_key_create (pthread_key_t *key, void (*destructor) (void*))
{
  if (key == NULL)
    return EINVAL;

  __pthread_disable_ints ();
  struct __pthread_key *newkey = malloc_unlocked (__ul_global.malloc_state,
						  sizeof (struct __pthread_key));
  int retval;
  if (newkey == NULL)
    retval = ENOMEM;
  else if (nextkey >= PTHREAD_KEYS_MAX)
    retval = EAGAIN;
  else
    {
      newkey->next = keys;
      keys = newkey;
      *key = newkey->keyid = nextkey++;
      newkey->destructor = destructor;
      retval = 0;
    }
  __pthread_enable_ints ();

  return retval;
}

/* Deletes a thread specific key (does not call the key's destructors).  */
int
pthread_key_delete (pthread_key_t key)
{
  struct __pthread_key *oldkey; /* Key to delete.  */
  int retval;

  if (keys == NULL)
    return EINVAL;

  retval = 0;
  __pthread_disable_ints ();

  /* Check whether key is head node in list of keys.  */
  if (keys->keyid == key)
    {
      /* It is; remove key from the linked list of keys.  */
      oldkey = keys;
      keys = keys->next;
    }
  else
    {
      /* Key is buried somewhere within the list.  */
      struct __pthread_key *keylist = keys;
      while (keylist->next && keylist->next->keyid != key)
        keylist = keylist->next;

      if (keylist->next == NULL)
        {
	  /* Key doesn't exist.  */
	  retval = EINVAL;
	}
      else
	{
	  oldkey = keylist->next;
	  keylist->next = oldkey->next;
	}
    }

  /* If retval is zero, then it indicates that we successfully
     found a key.  */
  if (! retval)
    free_unlocked (__ul_global.malloc_state, oldkey);

  __pthread_enable_ints ();
  return retval;
}

/* Set a thread specific value to associate with the given key.  */
int
pthread_setspecific (pthread_key_t key, const void *value)
{
  struct __pthread_key *keylist;
  struct __pthread_key *findkey;

  __pthread_disable_ints ();

  /* Find key in linked list.  */
  keylist = keys;
  while (keylist != NULL && keylist->keyid != key)
    keylist = keylist->next;

  if (keylist == NULL)
    {
      __pthread_enable_ints ();
      return EINVAL;
    }

  /* Check to see if it's already set for this thread.  */
  findkey = __pthread_running_thread->keys;
  while (findkey != NULL && findkey->keyid != key)
    findkey = findkey->next;

  if (findkey == NULL)
    {
      findkey = malloc_unlocked (__ul_global.malloc_state,
				 sizeof (struct __pthread_key));
      if (findkey == NULL)
	{
	  __pthread_enable_ints ();
	  return ENOMEM;
	}

      /* Add key to list for this thread */
      findkey->next = __pthread_running_thread->keys;
      __pthread_running_thread->keys = findkey;

      findkey->keyid = key;
    }

  findkey->value.constvalue = value;
  findkey->destructor = keylist->destructor;

  __pthread_enable_ints ();
  return 0;
}

/* Read value associated with given key for the current thread.
   Returns NULL if no value has been set for this thread yet.

   2026 (P11): every C++ thread_local and __thread variable comes here
   (GCC's emulated TLS keeps them all under one key), so the node found
   last is remembered and tried first.  A thread's key list is changed
   only by that thread (pthread_setspecific adds to its front, and
   __pthread_key_calldestructors takes nodes off as it exits, forgetting
   the remembered one), so it is read here without holding thread
   switching off: a switch can't change it under us.  */
void *
pthread_getspecific (pthread_key_t key)
{
  pthread_t self = __pthread_running_thread;
  struct __pthread_key *keylist = self->key_cache;

  if (keylist != NULL && keylist->keyid == key)
    return keylist->value.nonconst;

  for (keylist = self->keys; keylist != NULL; keylist = keylist->next)
    if (keylist->keyid == key)
      {
	self->key_cache = keylist;
	return keylist->value.nonconst;
      }

  return NULL;
}
