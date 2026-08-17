/* ============================================================
 * kernel/arch/arm/proc/context_switch.S — bidirectional switch
 *
 * void context_switch(thread_t *prev, thread_t *next);
 *
 * Saves prev's kernel state (callee-saved GPRs + lr pushed on
 * prev's SVC stack, plus banked SP_usr/LR_usr into prev->ctx)
 * then loads next's equivalents. Swaps TTBR0 and flushes TLB
 * between save and load.
 *
 * The switched-to address space comes from next->proc->pgd_pa —
 * threads borrow the page table of the process that owns them.
 *
 * prev == NULL is a legal "no-save, load-only" form used by the
 * very first call from kmain to bootstrap threads[0]. In that
 * case the epilogue still pops next's 9-word kernel-resume frame
 * and bx lr's to ret_from_first_entry (pre-baked into the
 * initial stack), which drains the 16-word IRQ-exit frame and
 * atomically enters USR mode.
 *
 * Caller is expected to be in SVC mode with CPSR.I already
 * matching the desired interrupt state; this routine does not
 * change the interrupt mask.
 *
 * Struct offsets are duplicated here and checked by _Static_assert
 * in kernel/include/proc.h — drift triggers a compile error.
 * =========================================================== */

    .equ CTX_SP_SVC_OFFSET,     52
    .equ CTX_SP_USR_OFFSET,     64
    .equ CTX_LR_USR_OFFSET,     68
    .equ THREAD_PROC_OFFSET,    72
    .equ PROC_PGD_PA_OFFSET,    12

    .section .text.ctxsw, "ax"
    .align  4
    .global context_switch

context_switch:
    /* r0 = prev (may be NULL), r1 = next */

    /* ----- save side ------------------------------------- */
    cmp     r0, #0
    beq     .Lload_next

    /* Push callee-saved GPRs + return address onto prev's SVC
     * stack. 9 words total; sp is now at the top of that frame. */
    push    {r4-r11, lr}
    str     sp, [r0, #CTX_SP_SVC_OFFSET]

    /* Save prev's banked USR-mode SP and LR. They are only
     * accessible from a mode that shares the USR register bank
     * (SYS mode, CPSR.M = 0x1F). */
    cps     #0x1F
    str     sp, [r0, #CTX_SP_USR_OFFSET]
    str     lr, [r0, #CTX_LR_USR_OFFSET]
    cps     #0x13

.Lload_next:
    /* ----- swap address space ---------------------------- */
    /* The page table belongs to the PROCESS, not the thread, so
     * reach it through next->proc. Two threads of one process
     * resolve to the same pgd_pa here — a future optimisation can
     * turn that into an early-out that skips the TLB/I-cache
     * flush entirely when prev and next share a process. */
    ldr     r2, [r1, #THREAD_PROC_OFFSET]   @ r2 = next->proc
    ldr     r2, [r2, #PROC_PGD_PA_OFFSET]   @ r2 = next->proc->pgd_pa
    orr     r2, r2, #0x4A                   @ TTBR0 walk attrs
    mcr     p15, 0, r2, c2, c0, 0
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0           @ TLBIALL
    mcr     p15, 0, r2, c7, c5, 0           @ ICIALLU — stale I-cache
                                            @ from previous process's
                                            @ user VA must not leak.
    dsb
    isb

    /* ----- load next ------------------------------------- */
    /* Restore banked USR regs before leaving SYS mode. */
    cps     #0x1F
    ldr     sp, [r1, #CTX_SP_USR_OFFSET]
    ldr     lr, [r1, #CTX_LR_USR_OFFSET]
    cps     #0x13

    /* Swap to next's kernel stack. From here on sp references
     * whatever context_switch left on next's SVC stack last
     * time it paused — either a real 9-word resume frame, or
     * the pre-built one from thread_build_initial_frame. */
    ldr     sp, [r1, #CTX_SP_SVC_OFFSET]

    /* Pop r4-r11 + lr (9 words). For first-time entries, lr
     * ends up pointing at ret_from_first_entry, which finishes
     * the job by draining the 16-word IRQ-exit frame. */
    pop     {r4-r11, lr}
    bx      lr
