@ RISC OS for Linux (the Linux port of RISC OS) support.
@ Copyright (c) 2026 UnixLib Developers
@
@ The Linux port runs RISC OS in a Linux user-mode process: the processor
@ is always in USR mode, and RISC OS's own modes (SVC, IRQ, ABT, UND) and
@ their banked registers are kept in software. So these instructions
@ don't do what they do on a real machine:
@
@	MSR CPSR_c		ignored in USR mode: no mode or IRQ change
@	MRS/MSR SPSR		undefined in USR mode
@	STM/LDM {..}^		the "user bank" is the current bank
@	MOVS pc, lr		no SPSR to restore from
@
@ The port's HAL registers a device (type &30A, ID 1, "Linux HAL PSR
@ Manipulation") whose descriptor holds routines that do these jobs in
@ software. sys/_syslib.s looks for it at start-up (OS_Hardware 5) and
@ stores the descriptor's address in __ul_global.pvops; on any other
@ machine there is no such device and pvops is 0. The code that needs
@ these instructions checks pvops and calls the routines instead:
@
@  PV_GETCPSR		out: r0 = CPSR, with the virtual mode and I/F bits
@  PV_GETSPSR		out: r0 = SPSR of the current (virtual) mode
@  PV_LOADUSR		in: r1 -> 2 words; sets USR r13, r14
@  PV_LOADUSRREGS	in: r1 -> r0-r12, r13_usr, r14_usr, pc;
@			    r2 = sp to keep for the current mode; r3 = CPSR.
@			Doesn't return: continues in the mode and at the pc
@			given (as MSR SPSR + LDM ^ + MOVS pc, lr).
@  PV_SETCPSR_C_UNSTACK	in: stack = {r0-r3, ip, lr} (just pushed), r1 = new
@			CPSR_c byte. Returns to lr + 4 with r0-r3, ip
@			restored, and sp, lr from the new mode's bank.
@  PV_STOREUSR		in: r1 -> 2 words; stores USR r13, r14
@
@ Each preserves the registers it doesn't name (lr is used for the call).
@ PV_SETCPSR_C_UNSTACK does nothing in USR mode (use OS_EnterOS first).
@
@ Only the EABI build is converted: signal/_signal.s's non-EABI (APCS)
@ exception handlers still use STM ^ and MRS SPSR, so an APCS program on
@ the port would still hang on a fault there.

.set	PV_DEVICE_TYPE, 0x30A	@ HAL device type: system peripheral, PSR
.set	PV_DEVICE_ID, 1		@ the Linux port's
.set	PV_GETCPSR, 80
.set	PV_GETSPSR, 84
.set	PV_LOADUSR, 92
.set	PV_LOADUSRREGS, 96
.set	PV_SETCPSR_C_UNSTACK, 108
.set	PV_STOREUSR, 124

	@ Call routine OFFSET of the descriptor in register PV. Corrupts lr.
	.macro	PV_CALL pv, offset
	mov	lr, pc
	ldr	pc, [\pv, #\offset]
	.endm

	@ Change mode as CHGMODE does, through the descriptor in PV (not
	@ sp or lr). Preserves r0-r3 and ip (not, necessarily, the flags);
	@ sp and lr become the new mode's, as with MSR CPSR_c.
	.macro	PV_CHGMODE pv, mode
	stmfd	sp!, {r0-r3, ip, lr}
	mov	ip, \pv
	mov	r1, #(0x10 | ((\mode) & 0xf) | ((\mode) >> 20))
	mov	lr, pc
	ldr	pc, [ip, #PV_SETCPSR_C_UNSTACK]
	nop				@ skipped: the routine returns to lr + 4
	.endm

	@ CHGMODE, or PV_CHGMODE if PV (a register) isn't 0. SCRATCH may be
	@ the same register as PV. Corrupts the flags.
	.macro	CHGMODE_PV scratch, mode, pv
	teq	\pv, #0
	bne	.Lpv_chg\@
	CHGMODE	\scratch, \mode
	b	.Lpv_chg_done\@
.Lpv_chg\@:
	PV_CHGMODE \pv, \mode
.Lpv_chg_done\@:
	.endm
