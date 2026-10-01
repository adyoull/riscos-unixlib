@ Thread ticker routines, shared by the PThreadTicker module and UnixLib
@ Copyright (c) 2026 UnixLib Developers
@
@ TICKER_ROUTINES p assembles five routines, all called with ip (r12) =
@ the program's pthread RMA block (struct __pthread_callevery_block,
@ PTHREAD_CALLEVERY_RMA_* in asm_dec.s):
@
@   p_handler    the OS_CallEvery handler
@   p_start      start the ticker if it isn't running
@   p_stop       stop it if it is
@   p_prefilter  Wimp pre-filter: note that the program is in Wimp_Poll
@   p_postfilter Wimp post-filter: Wimp_Poll is returning; switch threads
@                now if a tick came meanwhile
@
@ 2026 (5.0.3.1): the filters used to stop the ticker in the pre-filter and
@ start it again in the post-filter. Starting it began a new 2 cs period,
@ so a program that called Wimp_Poll more often than every 2 cs never got
@ a tick, and its other threads never ran (TickerTest on a Pi: 600000
@ polls in 20 s, both threads counted 0). Now the ticker runs all the
@ time. While the program is in Wimp_Poll (between the filters) a tick
@ sets no callback, which could be taken by whichever task Wimp_Poll
@ returns to; it sets 'pending', and the post-filter, when Wimp_Poll
@ returns to this program, sets the callback instead.
@
@ The PThreadTicker module (module/pthticker.s) assembles them, so they
@ are always paged in. Without that module, UnixLib copies them into the
@ RMA block at start-up and runs the copy (pthread/_context.s).
@ Either way they must not depend on where they are: only ip-relative
@ data, ADR/BL within this block, SWIs; no literal pools, no branches out.
@ The block offsets they use (76, 80, 88, 92, 96, 120-143 and, from
@ interface version 2 / module 0.03, 152-163) are an interface between
@ UnixLib and the module, which are released separately: don't move them.
@
@ The handler may run while another task is paged in, and the Filter
@ module can call the filters for another task (docs/THREAD-TICKER.md,
@ H1). So each checks that the upcall handler and its R12 are the
@ program's (from SUL) before acting; a tick that finds another task paged
@ in only counts (and notes 'pending' if the program is in Wimp_Poll).
@
@ Syntax: keep it valid in both divided and unified syntax (no
@ conditional LDM/STM), so any module source can use it.

.macro	TICKER_ROUTINES p

	@ OS_CallEvery handler: every 2 cs, SVC or IRQ mode, IRQs disabled.
	@ Preserves all registers.
\p\()_handler:
	STMFD	sp!, {a1-a4, lr}
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKS]
	BL	\p\()_ours
	BVS	\p\()_handler_out
	BNE	\p\()_handler_foreign

	@ Ours. In Wimp_Poll: leave it to the post-filter.
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
	TEQ	a1, #0
	BNE	\p\()_handler_pending

	@ Set the callback unless in a context switch or a critical section.
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_CALLBACK_SEMAPHORE]
	TEQ	a1, #0
	LDREQ	a1, [ip, #PTHREAD_CALLEVERY_RMA_WORKSEMAPHORE]
	TEQEQ	a1, #0
	SWIEQ	XOS_SetCallBack
\p\()_handler_out:
	LDMFD	sp!, {a1-a4, pc}

	@ Another task is paged in: count it, note whose upcall handler.
\p\()_handler_foreign:
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_FOREIGN_TICKS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_FOREIGN_TICKS]
	STR	a2, [ip, #PTHREAD_CALLEVERY_RMA_FOREIGN_HANDLER]
	STR	a3, [ip, #PTHREAD_CALLEVERY_RMA_FOREIGN_R12]
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
	TEQ	a1, #0
	BEQ	\p\()_handler_out
\p\()_handler_pending:
	MOV	a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PENDING]
	LDMFD	sp!, {a1-a4, pc}

	@ Is the program paged in? Reads the upcall handler (OS_ChangeEnvironment
	@ 16): SUL's, with the program's key as its R12, when it is.
	@ Returns V set if the SWI failed, else Z set if ours (a2, a3 = the
	@ handler and R12 found). Corrupts a1-a4. lr is saved on the stack:
	@ a SWI in SVC mode overwrites it.
\p\()_ours:
	STMFD	sp!, {lr}
	MOV	a1, #16
	MOV	a2, #0
	MOV	a3, #0
	MOV	a4, #0
	SWI	XOS_ChangeEnvironment
	BVS	\p\()_ours_out
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_UPCALL_ADDR]
	TEQ	a1, a2
	LDREQ	a1, [ip, #PTHREAD_CALLEVERY_RMA_UPCALL_R12]
	TEQEQ	a1, a3
\p\()_ours_out:
	LDMFD	sp!, {pc}		@ flags as set above

	@ Start the ticker. Any mode; preserves all registers and flags,
	@ returns no errors.
\p\()_start:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
	MOV	a1, #0
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
	TEQ	a1, #0
	BNE	\p\()_start_out
	MOV	a1, #1			@ Every 2 cs
	ADR	a2, \p\()_handler
	MOV	a3, ip
	SWI	XOS_CallEvery
	MOVVC	a1, #1
	STRVC	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
\p\()_start_out:
	MSR	CPSR_f, a4
	LDMFD	sp!, {a1-a4, pc}

	@ Stop the ticker. As \p\()_start.
\p\()_stop:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
	MOV	a1, #0
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PENDING]
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
	TEQ	a1, #0
	BEQ	\p\()_stop_out
	ADR	a1, \p\()_handler
	MOV	a2, ip
	SWI	XOS_RemoveTickerEvent
	MOV	a1, #0
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
\p\()_stop_out:
	MSR	CPSR_f, a4
	LDMFD	sp!, {a1-a4, pc}

	@ Wimp filters (Filter_RegisterPreFilter/PostFilter with R2 = the
	@ block), SVC mode. Preserve all registers and flags. Called for
	@ another task (FilterManager, H1): only count.
\p\()_prefilter:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
	STMFD	sp!, {a4}
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PRE_CALLS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PRE_CALLS]
	@ IRQs off from the check to setting 'polling', so a tick can't set
	@ a callback in between (restored at _filter_out).
	ORR	a1, a4, #IFlag32
	MSR	CPSR_c, a1
	BL	\p\()_ours
	BVS	\p\()_filter_out
	BNE	\p\()_filter_out
	MOV	a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
\p\()_filter_out:
	LDMFD	sp!, {a4}
	MSR	CPSR_cf, a4
	LDMFD	sp!, {a1-a4, pc}

\p\()_postfilter:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
	STMFD	sp!, {a4}
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_CALLS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_CALLS]
	BL	\p\()_ours
	BVS	\p\()_post_start
	BNE	\p\()_post_start
	MOV	a1, #0
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POLLING]
	LDR	a2, [ip, #PTHREAD_CALLEVERY_RMA_PENDING]
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PENDING]
	TEQ	a2, #0
	BEQ	\p\()_post_start
	@ A tick came while in Wimp_Poll: switch threads as it returns,
	@ unless in a context switch or a critical section.
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_CALLBACK_SEMAPHORE]
	TEQ	a1, #0
	LDREQ	a1, [ip, #PTHREAD_CALLEVERY_RMA_WORKSEMAPHORE]
	TEQEQ	a1, #0
	BNE	\p\()_post_start
	SWI	XOS_SetCallBack
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_SWITCHES]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_SWITCHES]
	@ Make sure the ticker runs (it always should while the filters are
	@ registered).
\p\()_post_start:
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
	TEQ	a1, #0
	BNE	\p\()_filter_out
	MOV	a1, #1
	ADR	a2, \p\()_handler
	MOV	a3, ip
	SWI	XOS_CallEvery
	MOVVC	a1, #1
	STRVC	a1, [ip, #PTHREAD_CALLEVERY_RMA_TICKER_STARTED]
	B	\p\()_filter_out
\p\()_end:
.endm
