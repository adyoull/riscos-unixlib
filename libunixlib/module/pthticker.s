@ PThreadTicker: runs UnixLib's thread ticker routines from a module
@ Copyright (c) 2026 UnixLib Developers
@
@ 2026 (riscos-unixlib): UnixLib switches threads with an OS_CallEvery
@ ticker, and a Wimp task stops it with Wimp filters while it is paged out.
@ The ticker can still fire while another task is paged in, and the
@ FilterManager can call the filters for the wrong task, so the routines
@ must not live in the program. A module is always paged in.
@
@ Without this module UnixLib copies the same routines into its RMA block
@ and runs them there; with it, UnixLib runs the module's copy. The
@ routines themselves are internal/ticker.s. See docs/THREAD-TICKER.md.
@
@ No SWIs (none are allocated to it). UnixLib finds the module with
@ OS_Module 18 ("PThreadTicker") and reads the interface table that
@ follows the module header (offset &34):
@
@   +0  "PTTk" (&6B545450)
@   +4  interface version (2 from 0.03; 1 in 0.01-0.02, used by UnixLib
@       before 5.0.3.1, which ignores this module and uses its own copy)
@   +8  number of entries that follow (7)
@   +12 offsets from the module start of:
@       handler, start, stop, pre-filter, post-filter  (ip = RMA block)
@       attach, detach                                 (ip = workspace)
@
@ attach/detach count the programs using the module; it refuses to be
@ killed while that count isn't 0. UnixLib attaches at start-up and
@ detaches at exit, after stopping its ticker and removing its filters.

#include "internal/asm_dec.s"
#include "internal/ticker.s"

	.syntax unified
	.text

module_start:
	.word	0				@ Start code
	.word	init_code - module_start	@ Initialisation
	.word	final_code - module_start	@ Finalisation
	.word	0				@ Service call handler
	.word	title - module_start		@ Title string
	.word	help - module_start		@ Help string
	.word	0				@ Command table
	.word	0				@ SWI chunk
	.word	0				@ SWI handler
	.word	0				@ SWI table
	.word	0				@ SWI decoder
	.word	0				@ Messages file
	.word	module_flags - module_start	@ Flags

	@ The interface table (see above); UnixLib expects it at &34
interface:
	.word	0x6B545450			@ "PTTk"
	.word	2				@ Interface version (0.03)
	.word	7				@ Entries
	.word	pt_handler - module_start
	.word	pt_start - module_start
	.word	pt_stop - module_start
	.word	pt_prefilter - module_start
	.word	pt_postfilter - module_start
	.word	pt_attach - module_start
	.word	pt_detach - module_start
	.if	interface - module_start - 0x34
	.error	"The interface table must follow the module header (&34)"
	.endif

title:
	.asciz	"PThreadTicker"
help:
	.asciz	"PThreadTicker\t0.03 (01 Oct 2026) riscos-unixlib"
	.align
module_flags:
	.word	1				@ 32-bit compatible

error_inuse:
	.word	0
	.asciz	"PThreadTicker is in use by UnixLib programs"
	.align

	@ The ticker routines: ip = the program's pthread RMA block
	TICKER_ROUTINES pt

	@ Count a program using the module. ip = workspace (OS_Module 18's
	@ R4). Preserves all registers and flags. Called from USR mode, or
	@ with IRQs off. 0.02: the count is updated with IRQs off (OS_IntOff),
	@ so two programs in TaskWindows can't both update it at once and
	@ lose a count.
pt_attach:
	STMFD	sp!, {a1, a2, lr}
	MOV	a1, #1
	B	pt_count

	@ ... and one that has finished with it.
pt_detach:
	STMFD	sp!, {a1, a2, lr}
	MVN	a1, #0
pt_count:
	MRS	a2, CPSR
	STMFD	sp!, {a2}
	TST	a2, #IFlag32
	SWIEQ	XOS_IntOff
	LDR	a2, [ip]
	ADDS	a2, a2, a1
	MOVMI	a2, #0
	STR	a2, [ip]
	LDMFD	sp!, {a2}
	TST	a2, #IFlag32
	SWIEQ	XOS_IntOn
	MSR	CPSR_f, a2
	LDMFD	sp!, {a1, a2, pc}

	@ Workspace: one word, the number of programs attached.
init_code:
	STMFD	sp!, {lr}
	LDR	r2, [r12]
	TEQ	r2, #0			@ Re-initialising: keep it
	BNE	init_done
	MOV	r0, #6
	MOV	r3, #4
	SWI	XOS_Module
	BVS	init_fail
	STR	r2, [r12]
	MOV	r0, #0
	STR	r0, [r2]
init_done:
	MSR	CPSR_f, #0		@ V clear: no error
	LDMFD	sp!, {pc}
init_fail:
	LDMFD	sp!, {lr}
	MSR	CPSR_f, #VFlag
	MOV	pc, lr

	@ Refuse to die while a program uses the routines: its ticker or
	@ filters would call freed code.
final_code:
	STMFD	sp!, {lr}
	LDR	r2, [r12]
	TEQ	r2, #0
	BEQ	final_done
	LDR	r0, [r2]
	TEQ	r0, #0
	BNE	final_inuse
	MOV	r0, #7
	SWI	XOS_Module
	MOV	r0, #0
	STR	r0, [r12]
final_done:
	MSR	CPSR_f, #0		@ V clear: no error
	LDMFD	sp!, {pc}
final_inuse:
	LDMFD	sp!, {lr}
	ADR	r0, error_inuse
	MSR	CPSR_f, #VFlag
	MOV	pc, lr

	.end
