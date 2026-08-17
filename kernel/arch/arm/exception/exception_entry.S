/* ============================================================
 * kernel/arch/arm/exception/exception_entry.S — Low-level
 * exception entry stubs
 *
 * Each stub:
 *   1. Adjusts LR to point at the faulting/interrupted instruction
 *   2. Saves {SPSR, r0-r12, LR} on the exception-mode stack
 *   3. Switches to SVC mode (unified C handler execution)
 *   4. Calls the C handler with r0 = pointer to saved context
 *
 * Context layout on stack (exception_context_t):
 *   [SP+0]  spsr
 *   [SP+4]  r0 .. [SP+52] r12
 *   [SP+56] lr  (adjusted return address)
 *
 * All fault handlers are fatal — dump registers and halt.
 * SVC returns to caller after printing the syscall number.
 * IRQ/FIQ halt (interrupts not enabled yet).
 *
 * Dependencies: exception_handlers.c (C handlers)
 * =========================================================== */

    .text
    .arm

    .global exception_entry_undef
    .global exception_entry_svc
    .global exception_entry_pabort
    .global exception_entry_dabort
    .global exception_entry_irq
    .global exception_entry_fiq

/* ===========================================================
 * Data Abort — LR_abt = PC+8, subtract 8 to get faulting instr
 * =========================================================== */
exception_entry_dabort:
    sub     lr, lr, #8              /* LR now points at faulting instruction */
    stmfd   sp!, {r0-r12, lr}      /* save r0-r12 + adjusted LR */
    mrs     r0, spsr
    stmfd   sp!, {r0}              /* save SPSR at bottom of frame */
    mov     r0, sp                  /* r0 = &exception_context_t */
    cps     #0x13                   /* switch to SVC mode */
    bl      handle_data_abort
    b       .                       /* fatal — never returns */

/* ===========================================================
 * Prefetch Abort — LR_abt = PC+4, subtract 4
 * =========================================================== */
exception_entry_pabort:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r12, lr}
    mrs     r0, spsr
    stmfd   sp!, {r0}
    mov     r0, sp
    cps     #0x13
    bl      handle_prefetch_abort
    b       .

/* ===========================================================
 * Undefined Instruction — LR_und = PC+4, subtract 4 to get
 * the undefined instruction address
 * =========================================================== */
exception_entry_undef:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r12, lr}
    mrs     r0, spsr
    stmfd   sp!, {r0}
    mov     r0, sp
    cps     #0x13
    bl      handle_undefined
    b       .

/* ===========================================================
 * SVC (Supervisor Call) — already in SVC mode when entering
 *
 * LR_svc = return address (next instruction after SVC).
 * No LR adjustment needed.
 * =========================================================== */
exception_entry_svc:
    stmfd   sp!, {r0-r12, lr}      /* save on SVC stack */
    mrs     r0, spsr
    stmfd   sp!, {r0}
    mov     r0, sp                  /* r0 = &exception_context_t */
    bl      handle_svc
    /* Return from SVC */
    ldmfd   sp!, {r0}              /* pop saved SPSR */
    msr     spsr_cxsf, r0          /* restore SPSR */
    ldmfd   sp!, {r0-r12, pc}^     /* restore regs + return, SPSR→CPSR */

/* ===========================================================
 * IRQ — LR_irq = interrupted_PC + 4
 *
 * We park the IRQ-mode stack as a pure trampoline: srsdb pushes
 * {LR_irq, SPSR_irq} directly onto the SVC stack, so all real
 * work happens in SVC mode on the kernel stack.
 *
 * rfefd at the end pops the return PC and SPSR back atomically,
 * restoring both mode and status.
 * =========================================================== */
exception_entry_irq:
    sub     lr, lr, #4              /* LR = interrupted instruction       */
    srsdb   sp!, #0x13              /* push {LR_irq, SPSR_irq} on SVC stk */
    cps     #0x13                   /* switch to SVC mode                 */
    stmfd   sp!, {r0-r12, lr}      /* save GPRs + SVC LR                 */
    bl      handle_irq
    ldmfd   sp!, {r0-r12, lr}      /* restore GPRs + SVC LR              */
    rfefd   sp!                     /* pop PC + CPSR → return + unmask    */

/* ===========================================================
 * FIQ — not used in Cortex-A-OS, just halt
 * =========================================================== */
exception_entry_fiq:
    b       .

/* ===========================================================
 * ret_from_first_entry — trampoline for first-time process entry
 *
 * context_switch's epilogue pops {r4-r11, lr} and bx lr's here.
 * Stack at entry points at the 16-word IRQ-exit frame that
 * process_build_initial_frame laid down: we drain it the same
 * way the IRQ path does and land in USR mode atomically.
 * =========================================================== */
    .global ret_from_first_entry
ret_from_first_entry:
    ldmfd   sp!, {r0-r12, lr}       /* pop r0..r12 + svc_lr placeholder */
    rfefd   sp!                     /* pop PC + CPSR → USR mode, atomic */
