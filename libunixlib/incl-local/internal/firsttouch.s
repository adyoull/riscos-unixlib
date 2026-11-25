@ 2026 (M3): touch a block's pages from ARM code before a VFP or NEON
@ instruction that moves more than 60 bytes writes or reads it.
@
@ ARMEABISupport maps stack pages in when they are first touched (and
@ mmap pages given back with madvise MADV_DONTNEED/MADV_FREE; ordinary
@ mmap memory, and so malloc's, is mapped when it is allocated).  With 1.07 and later on a kernel with AbortTrap, it
@ completes the access that touched the page by copying the data itself,
@ but its copy handles at most 60 bytes (15 words).  memcpy moves 64
@ bytes at a time with VLDM/VSTM, and setjmp stores d8-d15 (64 bytes),
@ so when one of those was the first touch of a page the program died:
@ an abort in SVC mode inside ARMEABISupport, or "Unixlib detected that
@ ARMEABISupport could not handle an abort" (EMT trap).  On a Pi 4 (RISC
@ OS 5.31, ARMEABISupport 1.08) a VSTM of 60 bytes to a fresh stack page
@ works, NEON VST1/VLD1 of 16 bytes and memset (32 bytes at a time) work,
@ and a VSTM or VLDM of 64 bytes, or memcpy of 64 bytes, kills the
@ program.  A load from each page of the block first, here in ARM code,
@ maps it the ordinary way.  Only loads: nothing is written, so another
@ thread's data is never at risk.  A page the block doesn't reach is
@ never touched, so nothing can fault here that the copy itself wouldn't.

@ For N (a3) bytes at PTR: load the first and the last byte (enough for
@ N <= 4096).  Uses ip and a4; a1-a3 and the other registers are kept.
@ N must not be 0.
.macro	first_touch_ends ptr
	ldrb	ip, [\ptr]
	add	a4, \ptr, a3
	ldrb	ip, [a4, #-1]
.endm

@ For N > 4096: load a byte from every page from the second to the one
@ before the last (the first and last are done by first_touch_ends).
@ Uses ip, a4 and (saved) v1; a1-a3 kept.  The last page is worked out
@ from the block's last byte, so nothing wraps past the top of memory.
.macro	first_touch_pages ptr
	push	{v1, lr}
	bic	v1, \ptr, #0xFF
	bic	v1, v1, #0xF00
	add	a4, \ptr, a3
	sub	a4, a4, #1
	bic	a4, a4, #0xFF
	bic	a4, a4, #0xF00
91:	add	v1, v1, #0x1000
	cmp	v1, a4
	ldrlob	ip, [v1]
	blo	91b
	pop	{v1, lr}
.endm
