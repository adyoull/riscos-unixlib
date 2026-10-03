/* pmovtest: a reproducer for ARMEABISupport losing a program whose page at
   &8000 is replaced (riscos-unixlib, 2026-10-03).

   ARMEABISupport identifies a program ("app") by the physical address of
   the page mapped at its &8000.  A dynamic area that claims a specific
   physical page (OS_DynamicArea 21, as drivers needing contiguous memory
   do after OS_Memory 12) can be given a page an application slot is
   using: the kernel copies it to another page and remaps it
   (Service_PagesUnsafe / Service_PagesSafe).  ARMEABISupport 1.08 has no
   service handler, so from then on it no longer finds the program; at
   exit its record and stacks are left behind (*ARMEABISupport_Info), and
   a later program that is given the old page at &8000 starts without
   abort handlers ("Unknown error code (6) from abort handler").

   This program claims, on purpose, the physical page that is at its own
   &8000, then releases it, and reports:
     - the page at &8000 before and after (it should have moved),
     - whether ARMEABISupport still finds this program (StackOp 2).
   Expected with ARMEABISupport 1.08: "moved" and "LOST".  Afterwards
   *ARMEABISupport_Info lists an App left by this run (reboot to clear).
   The program itself keeps working: only ARMEABISupport's bookkeeping is
   affected.  */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#define FLAG_NOT_DRAGGABLE 0x80
#define FLAG_SPECIFIC_PAGES (1u << 8)
#define FLAG_PMP (1u << 20)

/* A PMP area's handler: refuses every request.  */
__attribute__ ((naked)) static void
refuse_handler (void)
{
  __asm__ volatile ("MOV	r0, #0\n\t"
		    "CMP	r0, #1<<31\n\t"
		    "CMNVC	r0, #1<<31\n\t"
		    "ADR	r0, 1f\n\t"
		    "MOV	pc, lr\n"
		    "1:\t.word	0\n\t"
		    ".asciz	\"pmovtest area handler\"\n\t"
		    ".align	2\n");
}

/* OS_Memory 0 for the page at &8000: what = (1<<13) physical address,
   (1<<11) page number.  */
static unsigned page8000 (int what)
{
  int block[3] = { 0, 0x8000, 0 };
  if (_swix (OS_Memory, _INR(0,2), (1 << 9) | what, block, 1))
    return 0xFFFFFFFF;
  return (unsigned) block[what == (1 << 11) ? 0 : 2];
}

static int found (void)
{
  int handle = 0, here;
  if (_swix (0x59D02 /* ARMEABISupport_StackOp */, _INR(0,1) | _OUT(1),
	     2, &here, &handle))
    return 0;
  return handle != 0;
}

static const char *err (const _kernel_oserror *e)
{
  return e ? e->errmess : "";
}

int main (void)
{
  const _kernel_oserror *e;
  unsigned phys0 = page8000 (1 << 13), pnum = page8000 (1 << 11), phys1;
  int area = 0, f0 = found (), f1;
  unsigned base = 0, max = 0;
  int entry[3];

  printf ("pmovtest: page at &8000: physical &%08X, page number %u; "
	  "ARMEABISupport %s this program\n", phys0, pnum,
	  f0 ? "finds" : "DOESN'T FIND");
  if (pnum == 0xFFFFFFFF || !f0)
    {
      printf ("can't test (OS_Memory 0 failed, or ARMEABISupport not in use)\n");
      return 1;
    }

  e = _swix (OS_DynamicArea, _INR(0,9) | _OUT(1) | _OUT(3) | _OUT(5),
	     0, -1, 0, -1, FLAG_NOT_DRAGGABLE | FLAG_PMP | FLAG_SPECIFIC_PAGES,
	     4096, refuse_handler, 0, "pmovtest", 1, &area, &base, &max);
  if (e)
    {
      printf ("creating the area: %s\n", err (e));
      return 1;
    }
  /* Claim page index 0 = the physical page now at our &8000.  */
  entry[0] = 0;
  entry[1] = (int) pnum;
  entry[2] = 0;
  e = _swix (OS_DynamicArea, _INR(0,3), 21, area, entry, 1);
  if (e)
    printf ("claiming page %u: %s\n", pnum, err (e));
  phys1 = page8000 (1 << 13);
  f1 = found ();
  /* Give it back and remove the area.  */
  entry[0] = 0;
  entry[1] = -1;
  entry[2] = 0;
  _swix (OS_DynamicArea, _INR(0,3), 21, area, entry, 1);
  _swix (OS_DynamicArea, _INR(0,1), 1, area);

  printf ("after the claim: page at &8000 is physical &%08X (%s); "
	  "ARMEABISupport %s this program\n", phys1,
	  phys1 == phys0 ? "not moved" : "moved", f1 ? "finds" : "LOST");
  if (phys1 != phys0 && !f1)
    printf ("REPRODUCED: ARMEABISupport has lost this program; "
	    "*ARMEABISupport_Info will list an App left by it\n");
  else if (phys1 == phys0)
    printf ("NOT REPRODUCED: the page wasn't moved (claim refused?)\n");
  else
    printf ("OK: the page moved and ARMEABISupport followed it\n");
  return 0;
}
