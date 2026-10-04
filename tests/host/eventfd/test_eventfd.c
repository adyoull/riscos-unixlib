/* Host test for unix/eventfd.c: the counter is only loaded and stored with
   thread switching held off, the hold is always released (and released
   before pthread_yield), and read/write/select keep their meaning.  */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/eventfd.h>

#include <internal/fd.h>
#include <internal/unix.h>

extern int __eventfd_read (struct __unixlib_fd *, void *, int);
extern int __eventfd_write (struct __unixlib_fd *, const void *, int);
extern int __eventfd_select (struct __unixlib_fd *, int, fd_set *, fd_set *,
			     fd_set *);
extern int __eventfd_close (struct __unixlib_fd *);

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
  printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* Thread-switch hold: the work semaphore, as the real one.  */
static struct __pthread_callevery_block rma;
#define depth (rma.pthread_worksemaphore)
static int max_depth, holds, bad_enable, yield_while_held;
int __pthread_disable_ints (void)
{
  holds++;
  if (++depth > max_depth)
    max_depth = depth;
  return 0;
}
int __pthread_enable_ints (void)
{
  if (depth == 0)
    bad_enable++;
  else
    depth--;
  return 0;
}

/* pthread_yield: "another thread" runs a callback.  */
static void (*other_thread) (void);
static int yields;
void pthread_yield (void)
{
  yields++;
  if (depth != 0)
    yield_while_held++;
  if (other_thread)
    other_thread ();
}

static int cancels;
void pthread_testcancel (void) { cancels++; }

static void *fake_malloc (int pid, int size) { (void) pid; return malloc (size); }
static void fake_free (int pid, void *p) { (void) pid; free (p); }
static struct __sul_process proc = { 1, sizeof (struct __unixlib_fd_handle),
				     fake_malloc, fake_free };
struct ul_global __ul_global = { &proc, 1, &rma, NULL };

static struct __unixlib_fd fds[4];
int __alloc_file_descriptor (int start) { (void) start; return 3; }
struct __unixlib_fd *getfd (int fd) { return &fds[fd]; }
int __set_errno (int e) { errno = e; return -1; }

static struct __unixlib_fd *efd;

static void other_writes_2 (void)
{
  eventfd_t two = 2;
  other_thread = NULL;
  __eventfd_write (efd, &two, sizeof two);
}

/* Another thread's PTHREAD_UNSAFE call replaces the hold's return
   address while the reader waits.  */
static void other_writes_2_unsafe (void)
{
  __ul_global.pthread_return_address = (void *) 0xDEAD;
  other_writes_2 ();
}

static void other_reads (void)
{
  eventfd_t v;
  other_thread = NULL;
  __eventfd_read (efd, &v, sizeof v);
}

static void reset (void)
{
  depth = max_depth = holds = bad_enable = yield_while_held = yields = 0;
}

int main (void)
{
  eventfd_t v;
  fd_set rd, wr;

  CHECK (eventfd (5, 0) == 3);
  efd = getfd (3);
  CHECK (*(eventfd_t *) efd->devicehandle->handle == 5);

  /* Plain read takes the whole count, under the hold.  */
  reset ();
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 5);
  CHECK (holds == 1 && depth == 0 && max_depth == 1 && !bad_enable);

  /* Write adds.  */
  reset ();
  v = 7;
  CHECK (__eventfd_write (efd, &v, sizeof v) == sizeof v);
  v = 3;
  CHECK (__eventfd_write (efd, &v, sizeof v) == sizeof v);
  CHECK (holds == 2 && depth == 0 && !bad_enable);
  CHECK (*(eventfd_t *) efd->devicehandle->handle == 10);

  /* select reads the 64-bit counter under the hold.  */
  reset ();
  FD_ZERO (&rd); FD_ZERO (&wr);
  CHECK (__eventfd_select (efd, 3, &rd, &wr, NULL) == 2 && FD_ISSET (3, &rd));
  CHECK (holds == 1 && depth == 0);

  /* Semaphore mode hands out 1 at a time.  */
  efd->dflag = EFD_SEMAPHORE | EFD_NONBLOCK;
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 1);
  CHECK (*(eventfd_t *) efd->devicehandle->handle == 9);

  /* Non-blocking read of 0 gives EAGAIN without yielding, hold released.  */
  efd->dflag = EFD_NONBLOCK;
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 9);
  reset ();
  errno = 0;
  CHECK (__eventfd_read (efd, &v, sizeof v) == -1 && errno == EAGAIN);
  CHECK (yields == 0 && depth == 0 && !bad_enable);

  /* Blocking read of 0 yields with the hold released, then sees the
     other thread's write.  */
  efd->dflag = 0;
  reset ();
  other_thread = other_writes_2;
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 2);
  CHECK (yields == 1 && !yield_while_held && depth == 0 && !bad_enable);

  /* The same through read (), which holds switching off for the whole
     call (PTHREAD_UNSAFE_CANCELLATION): the wait must release that hold
     around pthread_yield and give back read ()'s return address.  Before,
     this was "pthread_yield called with context switching disabled".  */
  reset ();
  depth = 1;
  __ul_global.pthread_return_address = (void *) 0x8123;
  other_thread = other_writes_2_unsafe;
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 2);
  CHECK (yields == 1 && !yield_while_held && !bad_enable);
  CHECK (depth == 1);
  CHECK (__ul_global.pthread_return_address == (void *) 0x8123);
  depth = 0;

  /* A nested hold (2: fwrite -> write, or a caller's own
     __pthread_disable_ints): the wait mustn't yield (fatal) and can't let
     the other side run, so EAGAIN.  (fread -> read is only 1 deep:
     nested PTHREAD_UNSAFE doesn't add to the hold.)  */
  reset ();
  depth = 2;
  other_thread = other_writes_2;
  errno = 0;
  CHECK (__eventfd_read (efd, &v, sizeof v) == -1 && errno == EAGAIN);
  CHECK (yields == 0 && depth == 2 && !bad_enable);
  other_thread = NULL;
  depth = 0;

  /* The blocking wait is a cancellation point.  */
  reset ();
  cancels = 0;
  other_thread = other_writes_2;
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && cancels == 1);

  /* Blocking write that would overflow yields, unheld, until a read.  */
  *(eventfd_t *) efd->devicehandle->handle = UINT64_MAX - 1;
  reset ();
  other_thread = other_reads;
  v = 1;
  CHECK (__eventfd_write (efd, &v, sizeof v) == sizeof v);
  CHECK (yields == 1 && !yield_while_held && depth == 0 && !bad_enable);
  CHECK (*(eventfd_t *) efd->devicehandle->handle == 1);

  /* The same through writev (), which holds switching off.  */
  *(eventfd_t *) efd->devicehandle->handle = UINT64_MAX - 1;
  reset ();
  depth = 1;
  __ul_global.pthread_return_address = (void *) 0x8456;
  other_thread = other_reads;
  v = 1;
  CHECK (__eventfd_write (efd, &v, sizeof v) == sizeof v);
  CHECK (yields == 1 && !yield_while_held && depth == 1 && !bad_enable);
  CHECK (__ul_global.pthread_return_address == (void *) 0x8456);
  depth = 0;

  /* Non-blocking overflowing write: EAGAIN.  UINT64_MAX: EINVAL.  */
  efd->dflag = EFD_NONBLOCK;
  *(eventfd_t *) efd->devicehandle->handle = UINT64_MAX - 1;
  reset ();
  errno = 0;
  CHECK (__eventfd_write (efd, &v, sizeof v) == -1 && errno == EAGAIN);
  v = UINT64_MAX;
  errno = 0;
  CHECK (__eventfd_write (efd, &v, sizeof v) == -1 && errno == EINVAL);
  CHECK (depth == 0 && !bad_enable);

  /* Short buffers: EINVAL.  */
  CHECK (__eventfd_read (efd, &v, 4) == -1 && errno == EINVAL);

  /* Before threads start there's no hold to take.  */
  __ul_global.pthread_system_running = 0;
  reset ();
  v = 1;
  *(eventfd_t *) efd->devicehandle->handle = 0;
  CHECK (__eventfd_write (efd, &v, sizeof v) == sizeof v);
  CHECK (__eventfd_read (efd, &v, sizeof v) == sizeof v && v == 1);
  CHECK (holds == 0 && !bad_enable);

  CHECK (__eventfd_close (efd) == 0 && efd->devicehandle->handle == NULL);

  printf ("eventfd: %d checks, %d failed\n", checks, failures);
  return failures != 0;
}
