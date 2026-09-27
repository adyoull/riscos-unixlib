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
@   p_prefilter  Wimp pre-filter: count, then p_stop
@   p_postfilter Wimp post-filter: count, then p_start
@
@ The PThreadTicker module (module/pthticker.s) assembles them, so they
@ are always paged in. Without that module, UnixLib copies them into the
@ RMA block at start-up and runs the copy (pthread/_context.s).
@ Either way they must not depend on where they are: only ip-relative
@ data, ADR within this block, SWIs; no literal pools, no branches out.
@ The block offsets they use (76, 80, 88, 92, 96 and 120-143) are an
@ interface between UnixLib and the module, which are released
@ separately: don't move them.
@
@ Only the handler may run while another task is paged in. It checks that
@ the upcall handler and its R12 are the program's (from SUL) before it
@ touches anything but the RMA block, so a tick that finds another task
@ paged in does nothing but count.
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

	@ Read the current upcall handler: SUL's, with the program's key as
	@ its R12, when this program is paged in.
	MOV	a1, #16
	MOV	a2, #0
	MOV	a3, #0
	MOV	a4, #0
	SWI	XOS_ChangeEnvironment
	BVS	\p\()_handler_out
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_UPCALL_ADDR]
	TEQ	a1, a2
	LDREQ	a1, [ip, #PTHREAD_CALLEVERY_RMA_UPCALL_R12]
	TEQEQ	a1, a3
	BNE	\p\()_handler_foreign

	@ Ours. Set the callback unless in a context switch or a critical
	@ section.
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
	LDMFD	sp!, {a1-a4, pc}

	@ Start the ticker. Any mode; preserves all registers and flags,
	@ returns no errors (it may be a Wimp post-filter).
\p\()_start:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
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

	@ Stop the ticker. As \p\()_start (it may be a Wimp pre-filter).
\p\()_stop:
	STMFD	sp!, {a1-a4, lr}
	MRS	a4, CPSR
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
	@ block): count the call, then stop or start the ticker.
\p\()_prefilter:
	STMFD	sp!, {a1}
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PRE_CALLS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_PRE_CALLS]
	LDMFD	sp!, {a1}
	B	\p\()_stop

\p\()_postfilter:
	STMFD	sp!, {a1}
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_CALLS]
	ADD	a1, a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_POST_CALLS]
	LDMFD	sp!, {a1}
	B	\p\()_start
\p\()_end:
.endm
