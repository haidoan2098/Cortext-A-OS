/* ============================================================
 * kernel/arch/arm/boot/start.S — Cortex-A-OS reset entry point
 *
 * Linkage (see kernel/linker/kernel_*.ld):
 *   VMA (runtime) = 0xC0100000 (QEMU) / 0xC0000000 (BBB)
 *   LMA (load)    = 0x70100000 (QEMU) / 0x80000000 (BBB)
 *
 * Entry conditions:
 *   QEMU  — QEMU `-kernel` jumps to ELF e_entry = LMA, MMU off
 *   BBB   — SPL copies image to LMA, jumps there, MMU off
 *
 * Because every absolute symbol (`ldr rX, =sym`) resolves to VA
 * at link time, we cannot dereference any kernel symbol before
 * the MMU is on. The pre-MMU boot runs entirely on register
 * state + a tiny bit of SVC stack:
 *
 *     adr  r0, _start        @ PA of _start (PC-relative)
 *     ldr  r4, =_start       @ VA of _start
 *     sub  r4, r4, r0        @ PHYS_OFFSET = VA - PA
 *
 * C helpers invoked pre-MMU (mmu_init) must not touch string
 * literals or .data — see kernel/arch/arm/mm/mmu.c for notes.
 *
 * Sequence:
 *   1. Mask IRQ + FIQ.
 *   2. Compute PHYS_OFFSET into r4.
 *   3. Enter SVC mode, set SVC stack to PA (enough for mmu_init).
 *   4. Zero BSS at PA.
 *   5. mmu_init(phys_offset) — builds boot_pgd at PA, enables MMU.
 *   6. ldr pc, =_start_va  — absolute trampoline into high-VA.
 *   7. Set up every mode's stack at VA (MMU on now).
 *   8. bl kmain — kernel runs at VA from here on.
 * =========================================================== */

    .section .text.start, "ax"
    .align  4
    .global _start

_start:
    /* First instruction must decode as an ARM branch (0xEA......)
     * so the BBB SPL's kernel-image sanity check accepts it. A
     * relative `b` to _reset costs nothing and is harmless on QEMU. */
    b       _reset

_reset:
    /* 1. Mask interrupts ------------------------------------- */
    cpsid   if

    /* 2. Compute PHYS_OFFSET (r4) --------------------------- *
     *    adr gives PA of _start (PC-relative, MMU off).
     *    ldr =_start gives VA of _start (absolute literal).
     *    Difference is the virt-to-phys offset for this boot. */
    adr     r0, _start                      @ r0 = PA of _start
    ldr     r4, =_start                     @ r4 = VA of _start
    sub     r4, r4, r0                      @ r4 = VA - PA = PHYS_OFFSET

    /* 3. SVC mode + SVC stack (PA) --------------------------- */
    cps     #0x13                           @ SVC
    ldr     r1, =_svc_stack_top             @ VA
    sub     sp, r1, r4                      @ sp = PA of _svc_stack_top

    /* 4. Zero BSS (PA) --------------------------------------- */
    ldr     r1, =_bss_start                 @ VA
    ldr     r2, =_bss_end                   @ VA
    sub     r1, r1, r4                      @ PA of _bss_start
    sub     r2, r2, r4                      @ PA of _bss_end
    mov     r3, #0
.Lzero_bss:
    cmp     r1, r2
    strlo   r3, [r1], #4
    blo     .Lzero_bss

    /* 5. mmu_init(phys_offset) ------------------------------- *
     *    bl is PC-relative so this call stays at PA. mmu_init
     *    converts boot_pgd's VA to PA using r0 and populates
     *    the L1 table via that PA pointer, then enables MMU. */
    mov     r0, r4                          @ arg 0: phys_offset
    bl      mmu_init

    /* 6. VA trampoline --------------------------------------- *
     *    MMU is on (mmu_init enabled it). Identity map keeps PA
     *    fetches valid; the 0xC0... alias is also live. This
     *    absolute load pushes PC into the high-VA alias — from
     *    here every instruction fetch goes through the MMU. */
    ldr     pc, =_start_va

_start_va:
    /* 7. Set up every mode's stack at VA --------------------- *
     *    Interrupts stay masked (I/F bits untouched by cps #m). */
    cps     #0x11                           @ FIQ
    ldr     sp, =_fiq_stack_top

    cps     #0x12                           @ IRQ
    ldr     sp, =_irq_stack_top

    cps     #0x17                           @ ABT
    ldr     sp, =_abt_stack_top

    cps     #0x1B                           @ UND
    ldr     sp, =_und_stack_top

    cps     #0x13                           @ SVC — kernel mode
    ldr     sp, =_svc_stack_top

    /* 8. Enter the main kernel at VA ------------------------- */
    bl      kmain

    /* 9. Safety halt — kmain should not return --------------- */
.Lhalt:
    wfi
    b       .Lhalt
