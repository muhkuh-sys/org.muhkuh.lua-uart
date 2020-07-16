	.section .init_code, "ax"

	.extern test
	.extern stack_top

	.global start

#include "asic_types.h"

@--------------------------------------

@ The Cortex-M4 in the netX90 is a THUMB-only CPU.
@ All others start in 32 bit mode.
#if ASIC_TYP==ASIC_TYP_NETX90_MPW || ASIC_TYP==ASIC_TYP_NETX90
	.syntax unified
	.thumb
#else
	.arm
#endif


    .func start, start
    .type start, STT_FUNC
start:
@----------------------------------------------------------------------------
@
@ Save the registers and switch to the new stack.
@
	@ Save all working register.
	push    {r4, lr}

	@ Save the old stack position.
	mov     r3, sp

	@ Set own stack position.
	ldr     r2, =stack_top
	mov     sp, r2

	@ Store the old stack position on the new stack.
	push    {r3}


@----------------------------------------------------------------------------
@
@ Clear the .bss section (zero init).
@
	mov     r2, #0
	ldr     r3, =__bss_start__
	ldr     r4, =__bss_end__
clear_bss:
	str     r2, [r3], #4
	cmp     r3, r4
	bmi     clear_bss


@----------------------------------------------------------------------------
@
@ Replace the parameter in r0 with 0 if it does not look like a valid
@ pointer.
@
	@ The pointer must be 32 bit aligned.
	tst     r0, #3
	beq     parameter_valid
	@ The parameter is not valid.
	mov     r0, #0
parameter_valid:


@----------------------------------------------------------------------------
@
@ Call the main routine.
@
	@ Save the pointer to the parameter block.
	push    {r0}

	@ Get the test parameter.
	ldr     r0, [r0, #4]

	@ Call the main routine. It requires the pointer to the parameter block in r0.
	bl      test

	@ now the return value is in r0.


@----------------------------------------------------------------------------
@
@ Store the return value in the parameter block if the pointer is not 0.
@
	@ Restore the pointer to the parameter block.
	pop     {r1}
	cmp     r1, #0
	beq     skip_setting_the_return_value
	str     r0, [r1]
skip_setting_the_return_value:



@----------------------------------------------------------------------------
@
@ Restore the registers and switch to the old stack.
@
	@ Restore the old stack position.
	pop     {r4}
	mov     sp, r4

	@ Restore the working register.
	pop     {r4, lr}
	bx      lr

    .endfunc

@--------------------------------------

  .end
