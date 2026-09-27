@ Low level context switching code
@ Written by Martin Piper and Alex Waugh
@ Copyright (c) 2002-2012 UnixLib Developers

@ The context switcher works as follows:
@
@ OS_CallEvery is used to cause an interrupt every centisecond. This interrupt
@ could occur at any time, so we might be in the middle of a SWI call.
@ Therefore a callback is set, so when the SWI returns, or when the callevery
@ routine returns if a SWI call was not taking place, the callback handler is
@ invoked. (A transient callback cannot be used as this might be called when
@ RISC OS is idleing, eg. in an OS_ReadC call.)
@
@ The callback handler saves all integer and floating point register values
@ in the thread's context save area, and then calls the scheduler
@ __pthread_context_switch which decides which thread to run next. The
@ register values (including the saved program counter) from this new thread
@ are then loaded from the thread's context save area into the appropriate
@ registers, thus restoring that thread to where is was before it was switched
@ out, and returning from the callback at the same time.
@
@ If a routine does not want to be switched out, then it can call
@ __pthread_disable_ints or __pthread_protect_unsafe which will alter
@ __pthread_work_semaphore. The callevery interrupt will still occur, but it
@ will take note of the state of the semaphore, and not set a callback
@
@ Multitasking programs use the Filter module (Filter_RegisterPreFilter and
@ Filter_RegisterPostFilter) to enable/disable OS_CallEvery.

#include "internal/asm_dec.s"
#include "internal/ticker.s"


	.syntax unified
	.text

@ 2026: the ticker routines (starting, stopping, the OS_CallEvery handler
@ and the Wimp filters) are in internal/ticker.s. The PThreadTicker module
@ (module/pthticker.s) has its own copy; without it, __pthread_ticker_init
@ copies the ones below into the RMA block and runs them from there.
@ Either way they are paged in whichever task is: the ticker can fire while
@ another task is, and the Filter module can call the filters for the
@ wrong task. pthread/ticker.c decides which copy to use and manages the
@ filters.

	.global	__pthread_call_every_code
	.global	__pthread_call_every_code_end
	.global	__pthread_ticker_offsets
__pthread_call_every_code:
	TICKER_ROUTINES ul_ticker
__pthread_call_every_code_end:
	.if	__pthread_call_every_code_end - __pthread_call_every_code > 320
	.error	"The ticker routines don't fit in PTHREAD_CALLEVERY_RMA_TICKER_CODE"
	.endif
	.if	ul_ticker_handler - __pthread_call_every_code
	.error	"The ticker handler must come first"
	.endif

	@ Where the routines are in the copy: start, stop, pre-filter,
	@ post-filter.
__pthread_ticker_offsets:
	.word	ul_ticker_start - ul_ticker_handler
	.word	ul_ticker_stop - ul_ticker_handler
	.word	ul_ticker_prefilter - ul_ticker_handler
	.word	ul_ticker_postfilter - ul_ticker_handler

@ void __pthread_ticker_call (void *block, const void *routine)
@ Call one of the copied routines with ip = the RMA block.
	.global	__pthread_ticker_call
	NAME	__pthread_ticker_call
__pthread_ticker_call:
	STMFD	sp!, {a1, lr}
	MOV	ip, a1
	MOV	lr, pc
	MOV	pc, a2
	LDMFD	sp!, {a1, pc}
	DECLARE_FUNCTION __pthread_ticker_call

@ This is called from _signal.s::__h_cback and pthread_yield.
@ Entered in SVC or IRQ mode with IRQs disabled.

@ The operating system will eventually be returning to USR mode.
	.global	__pthread_callback

	NAME	__pthread_callback
__pthread_callback:
	PIC_LOAD v1
 
	LDR	a3, .L2			@=__ul_global
 PICEQ "LDR	a3, [v1, a3]"
 
	@ If we are in a critical region, do not switch threads and
	@ exit quicky.
	LDR	ip, [a3, #GBL_PTH_CALLEVERY_RMA]
	LDR	a1, [ip, #PTHREAD_CALLEVERY_RMA_WORKSEMAPHORE]

	TEQ	a1, #0
	@ If we are already in the middle of a context switch callback,
	@ then quickly exit.
	LDREQ	a1, [ip, #PTHREAD_CALLEVERY_RMA_CALLBACK_SEMAPHORE]
	TEQEQ	a1, #0
	BNE	skip_contextswitch

	@ Everything checks out, so from now on we're going to change
	@ contexts.

	@ Set __ul_global.pthread_callback_semaphore to ensure that another
	@ context interrupt does not interfere with us during this critical
	@ time.
	MOV	a1, #1
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_CALLBACK_SEMAPHORE]

	@ Setup a stack for the context switcher
	BL	__setup_signalhandler_stack

	@ Save regs to thread's save area
	LDR	a1, .L2+4	@=__pthread_running_thread
 PICEQ "LDR	a1, [v1, a1]"
	LDR	a1, [a1]
	LDR	a1, [a1, #__PTHREAD_CONTEXT_OFFSET]	@ __pthread_running_thread->saved_context

	@ Copy integer regs
	MOV	a2, ip		@ callback regs are stored at the start of the fast access RMA block
 PICEQ "MOV	ip, v1"		@ Save PIC register
	LDMIA	a2!, {a3, a4, v1, v2, v3, v4, v5, v6}
	STMIA	a1!, {a3, a4, v1, v2, v3, v4, v5, v6}
	LDMIA	a2!, {a3, a4, v1, v2, v3, v4, v5, v6}
	STMIA	a1!, {a3, a4, v1, v2, v3, v4, v5, v6}
	@ Copy SPSR
	LDR	a3, [a2]
 PICEQ "MOV	v1, ip"		@ Restore PIC register
	STR	a3, [a1], #4

	@ We have to copy the integer regs before switching IRQs
	@ back on, so they don't get overwritten by another callback,
	@ but the floating point instructions should only be called
	@ from user mode
	CHGMODE	a3, USR_Mode

#ifndef __SOFTFP__
#  ifdef __VFP_FP__
	@ Save the thread's VFP registers, and create a new context on the stack for the
	@ thread scheduler to use
	LDR	a3, .L2			@=__ul_global
 PICEQ "LDR	a3, [v1, a3]"
#    ifdef __ARM_EABI__
	MOV	a1, #0x40000001	@ User mode, dynamic area, lazy activate
#    else
	MOV	a1, #0x40000003	@ User mode, application space, lazy activate
#    endif
	LDR	a2, [a3, #GBL_VFP_REGCOUNT]
	SWI	VFPSupport_CheckContext
	MOV	v2, sp		@ Remember old SP
	SUB	a3, sp, a1
	BIC	a3, a3, #7	@ AAPCS wants 8 byte alignment
	MOV	sp, a3
#    ifdef __ARM_EABI__
	MOV	a1, #0x40000001	@ User mode, dynamic area, lazy activate
#    else
	MOV	a1, #0x40000003
#    endif
	MOV	a4, #0
	SWI	VFPSupport_CreateContext
#  else
	@ Save floating point regs
	SFM	f0, 4, [a1], #48
	SFM	f4, 4, [a1], #48
	RFS	a2	@ Read floating status
	STR	a2, [a1]
#  endif
#endif

	@ Call the scheduler to switch to another thread
	BL	__pthread_context_switch

	@ Now reload the registers from the new thread's save area
	LDR	a1, .L2+4	@=__pthread_running_thread
 PICEQ "LDR	a1, [v1, a1]"
	LDR	a1, [a1]
	LDR	a2, [a1, #__PTHREAD_CONTEXT_OFFSET]	@ __pthread_running_thread->saved_context

#ifndef __SOFTFP__
#  ifdef __VFP_FP__
	@ Destroy our temp context, and in the process switch to the target context
	LDR	a2, [a2, #17*4]
	MOV	a1, sp
	BIC	a2, a2, #1
	SWI	VFPSupport_DestroyContext
	MOV	sp, v2
#  else
	ADD	a2, a2, #17*4
	LFM	f0, 4, [a2], #48
	LFM	f4, 4, [a2], #48
	LDR	a1, [a2]
	WFS	a1	@ Write floating status
#  endif
#endif

	SWI	XOS_EnterOS	@ Back to supervisor mode

	CHGMODE	a2, SVC_Mode+IFlag	@ Force SVC mode, IRQs off

	LDR	a2, .L2		@=__ul_global
 PICEQ "LDR	a2, [v1, a2]"

	@ Indicate that this context switch was successful
	MOV	a1, #0
	STR	a1, [a2, #GBL_PTH_CALLBACK_MISSED]

	@ Signify that we are no longer in the middle of a context switch.
	LDR	ip, [a2, #GBL_PTH_CALLEVERY_RMA]
	STR	a1, [ip, #PTHREAD_CALLEVERY_RMA_CALLBACK_SEMAPHORE]

	@ Indicate that we are no longer in a signal handler, since we
	@ will be returning direct to USR mode and the application itself.
	LDR	a1, [a2, #GBL_EXECUTING_SIGNALHANDLER]
	SUB	a1, a1, #1
	STR	a1, [a2, #GBL_EXECUTING_SIGNALHANDLER]

	@ Point to the register save area for the new thread.
	LDR	r14, .L2+4	@=__pthread_running_thread
 PICEQ "LDR	r14, [v1, r14]"
	LDR	r14, [r14]
	LDR	r14, [r14, #__PTHREAD_CONTEXT_OFFSET]	@ __pthread_running_thread->saved_context

	@ Restore thread's registers
	LDR	a1, [r14, #16*4]	@ Get user PSR
	MSR	SPSR_cxsf, a1		@ Put it into SPSR_SVC/IRQ (NOP on ARM2/3, shouldn't have any effect in 26bit mode)
	LDMIA	r14, {r0-r14}^		@ Load USR mode regs
	MOV	a1, a1
	LDR	r14, [r14, #15*4]	@ Load the old PC value

	MOVS	pc, lr			@ Return (Valid for 26 and 32bit modes)


	@ Called because we are fast exiting from the context switcher
	@ because some other context switching operation is already going on,
	@ or because the program indicates that we are in a critical
	@ section.

	@ On entry, a3 = __ul_global
	@	    ip = ptr to fast access RMA block
skip_contextswitch:
	@ Indicate that this context switch did not occur
	MOV	a1, #1
	STR	a1, [a3, #GBL_PTH_CALLBACK_MISSED]

	@ Exiting from the CallBack handler requires us to reload all registers from the
	@ register save area which is at the beginning of the fast access RMA block
	LDR	a1, [r14, #16*4]	@ Get user PSR
	MSR	SPSR_cxsf, a1		@ Put it into SPSR_SVC/IRQ (NOP on ARM2/3, shouldn't have any effect in 26bit mode)
	LDMIA	r14, {r0-r14}^		@ Load USR mode regs
	MOV	a1, a1

	LDR	r14, [r14, #15*4]	@ Load the old PC value
	MOVS	pc, lr			@ Return (Valid for 26 and 32bit modes)
.L2:
	WORD	__ul_global
	WORD	__pthread_running_thread
	DECLARE_FUNCTION __pthread_callback

@ entry:
@   R0 = save area
	.global	__pthread_init_save_area
	NAME	__pthread_init_save_area
__pthread_init_save_area:
#ifndef __SOFTFP__
#  ifdef __VFP_FP__
	@ Allocate a VFP context from the heap
	@ Make sure we specify the 'application space' flag if we're not using a DA
	STMFD	sp!, {a1, v1, v2, lr}

	PIC_LOAD ip

	LDR	v2, .L3			@=__ul_global
 PICEQ "LDR	v2, [ip, v2]"

	LDR	a1, [v2, #GBL_DYNAMIC_NUM]
	CMP	a1, #-1
	MOVEQ	a1, #0x3	@ User mode, application space
	MOVNE	a1, #0x1	@ User mode, dynamic area
	LDR	a2, [v2, #GBL_VFP_REGCOUNT]
	MOV	v1, a1
	SWI	VFPSupport_CheckContext

	MOV	a2, a1
	LDR	a1, [v2, #GBL_MALLOC_STATE]
	BL	malloc_unlocked		@ TODO check for null

	MOV	a3, a1
	MOV	a1, v1
	LDR	a2, [v2, #GBL_VFP_REGCOUNT]
	MOV	a4, #0
	SWI	VFPSupport_CreateContext
	LDMFD	sp!, {a2, v1, v2, lr}
	STR	a1, [a2, #17*4]
#  else
	ADD	a2, a1, #17*4
	SFM	f0, 4, [a2], #48
	SFM	f4, 4, [a2], #48
	RFS	a1	@ Read floating status
	STR	a1, [a2], #12
#  endif
#endif
	MOV	pc, lr
.L3:
	WORD	__ul_global
	DECLARE_FUNCTION __pthread_init_save_area


	.end
