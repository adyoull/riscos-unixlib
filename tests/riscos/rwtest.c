/* rwtest: pthread_rwlock_init on memory that held another lock
   (riscos-unixlib P8, from riscos-unixlib-web W11).

   New memory that happens to look like a lock (here: the words of
   PTHREAD_RWLOCK_INITIALIZER, as memory freed with a lock in it that was
   never destroyed, which malloc then hands out again) is initialised:
   GLib's g_rw_lock does this with malloc'd memory and aborts if
   pthread_rwlock_init fails.  UnixLib used to say EBUSY because the type
   word read LOCK_READ.  The new lock must then work: two readers at
   once, then a writer.
   Must print PASS.  */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

int main (void)
{
  pthread_rwlock_t lock;
  int fails = 0, r;

  memset (&lock, 0, sizeof lock);
  if ((r = pthread_rwlock_init (&lock, NULL)) != 0)
    { printf ("rwtest: first init: %s\n", strerror (r)); fails++; }
  pthread_rwlock_rdlock (&lock);
  pthread_rwlock_unlock (&lock);
  {
    /* Memory holding a lock nobody destroyed.  */
    static const pthread_rwlock_t old = PTHREAD_RWLOCK_INITIALIZER;
    memcpy (&lock, &old, sizeof lock);
  }
  if ((r = pthread_rwlock_init (&lock, NULL)) != 0)
    { printf ("rwtest: init over memory that looks like a lock: %s (the old library says EBUSY)\n", strerror (r)); fails++; }
  if (pthread_rwlock_rdlock (&lock) != 0 || pthread_rwlock_rdlock (&lock) != 0)
    { printf ("rwtest: two read locks failed\n"); fails++; }
  if (pthread_rwlock_trywrlock (&lock) == 0)
    { printf ("rwtest: write lock taken while read-locked\n"); fails++; }
  pthread_rwlock_unlock (&lock);
  pthread_rwlock_unlock (&lock);
  if (pthread_rwlock_wrlock (&lock) != 0)
    { printf ("rwtest: write lock failed\n"); fails++; }
  pthread_rwlock_unlock (&lock);
  pthread_rwlock_destroy (&lock);

  puts (fails ? "rwtest: FAIL" : "rwtest: PASS");
  return fails != 0;
}
