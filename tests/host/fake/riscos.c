/* Fake RISC OS for the /dev/dsp host tests: SharedSoundBuffer + StreamManager
   playing in simulated time, SWI lookup, clock, yield. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "swis.h"
#include "riscos.h"

struct fake F;
int dr_state, dr_nbuf, dr_activations, dr_deactivations, dr_numbuf_calls, dr_streamed;
char *program_invocation_short_name = "testprog";
static _kernel_oserror err_full = { 1, "Buffer full" };
static _kernel_oserror err_noswi = { 2, "No such SWI" };
static _kernel_oserror err_bad = { 3, "Bad handle" };

void fake_reset (void)
{
  free (F.out);
  memset (&F, 0, sizeof F);
  F.modules = 1;
  F.out_cap = 1 << 22;
  F.out = malloc (F.out_cap);
}

static void play (long us)
{
  if (!F.open || F.paused || F.stall)
    return;
  F.frac += (double) F.rate * 4 * us / 1e6;
  while (F.frac >= 4 && F.played < F.added)
    { F.played += 4; F.frac -= 4; }
  if (F.played >= F.added)
    {
      if (F.frac >= 4) F.underruns_us += us;
      F.frac = 0;
    }
  if (F.frac > 4) F.frac = 4;
}

int fake_yield (void) { F.now_us += 1000; F.yields++; play (1000); return 0; }
long fake_clock (void) { return (long) (F.now_us / 10000); }
char *fake_getenv (const char *n) { return strcmp (n, "UnixLib$DSP") == 0 ? F.env_dsp : NULL; }
int __ul_seterr (const _kernel_oserror *e, int en) { (void) e; errno = en; return -1; }
void __pthread_enable_ints (void) { F.ints_toggles++; }
void __pthread_disable_ints (void) {}

const _kernel_oserror *_swix (int swi, unsigned mask, ...)
{
  int in[10] = {0}, *out[10] = {0}, r[10];
  va_list ap; int i;
  va_start (ap, mask);
  for (i = 0; i < 10; i++) if (mask & (1U << i)) in[i] = va_arg (ap, int);
  for (i = 0; i < 10; i++) if (mask & (1U << (31 - i))) out[i] = va_arg (ap, int *);
  va_end (ap);
  memcpy (r, in, sizeof r);
  swi &= ~0x20000;
  switch (swi)
    {
    case OS_SWINumberFromString:
      {
        const char *n = (const char *) (long) in[1];
        if (!F.modules && (strncmp (n, "SharedSoundBuffer", 17) == 0 || strncmp (n, "StreamManager", 13) == 0))
          return &err_noswi;
        r[0] = 1; break;
      }
    case 0x55FC0:
      F.open = 1; F.opens++; F.paused = 1; F.added = F.played = 0; F.frac = 0;
      F.blocksize = in[2]; snprintf (F.name, sizeof F.name, "%s", (char *) (long) in[1]);
      r[0] = 0x1000; break;
    case 0x55FC1: if (in[0] != 0x1000) return &err_bad; F.open = 0; F.closes++; break;
    case 0x55FC4: F.volume = (unsigned) in[1]; break;
    case 0x55FC5: F.rate = in[1] / 1024; break;
    case 0x55FC9: F.paused = in[1] == 0; if (!F.paused && !F.first_unpause_us) F.first_unpause_us = F.now_us + 1; break;
    case 0x55FCE: r[0] = 0x2000; break;
    case 0x57287: F.sm_limit = in[1]; break;
    case 0x57282:
      if (in[0] != 0x2000 || !F.open) return &err_bad;
      if (F.added - F.played + in[2] > F.sm_limit) return &err_full;
      if (F.out_len + in[2] <= F.out_cap)
        { memcpy (F.out + F.out_len, (void *) (long) in[1], in[2]); F.out_len += in[2]; }
      F.added += in[2]; F.blocks++;
      if (F.added - F.played > F.max_queued) F.max_queued = F.added - F.played;
      break;
    case 0x57288: r[0] = F.added; r[1] = F.played; break;
    default: fprintf (stderr, "unexpected SWI %x\n", swi); abort ();
    }
  for (i = 0; i < 10; i++) if (out[i]) *out[i] = r[i];
  return NULL;
}
