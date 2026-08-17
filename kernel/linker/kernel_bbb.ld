/* ===========================================================
 * kernel/linker/kernel_bbb.ld — Cortex-A-OS Kernel Linker Script
 *
 * Target: BeagleBone Black (AM335x)
 *   Physical DDR base : 0x80000000 (512 MB)
 *   Kernel load PA    : 0x80000000  (SPL loads kernel here)
 *   Kernel link VA    : 0xC0000000  (3G/1G split, Linux ARM style)
 *
 * ELF layout: every section has split VMA/LMA.
 *   VMA (VIRT region) → runtime addresses, symbols emit here
 *   LMA (PHYS region) → SPL copies bytes here
 *
 * Boot sequence expectation (start.S):
 *   1. Enter at PA 0x80000000 (MMU off). Absolute symbol loads
 *      resolve to VA — must subtract PHYS_OFFSET before use.
 *   2. mmu_enable() turns on MMU with identity + high-VA mapping.
 *   3. Trampoline PC to VA via `ldr pc, =kmain`.
 * =========================================================== */

/* ELF entry point = PA of _start.
 * SPL jumps to a physical address after copying the image; MMU is
 * off at that point so PC must be PA. `_start` symbol resolves to
 * VMA 0xC0000000; override e_entry to the matching PA.           */
ENTRY(_start_phys)

MEMORY
{
    /* PHYS — where SPL copies the image (LMA) */
    PHYS (rwx) : ORIGIN = 0x80000000, LENGTH = 512M

    /* VIRT — where symbols resolve at runtime (VMA) */
    VIRT (rwx) : ORIGIN = 0xC0000000, LENGTH = 512M
}

SECTIONS
{
    /* -------------------------------------------------------
     * .text — executable code
     *   .text.start must be first so _start lands at ORIGIN
     * ------------------------------------------------------- */
    .text :
    {
        _text_start = .;
        *(.text.start)      /* _start — must be first          */
        . = ALIGN(32);
        *(.vectors)         /* exception vector table           */
        *(.text*)
        *(.rodata*)
        /* Align to 16 so the next section (which has its own
         * 16-byte alignment requirement on VMA) starts at a
         * boundary that matches its LMA — otherwise AT> PHYS
         * can drift by a few bytes relative to VMA. */
        . = ALIGN(16);
        _text_end = .;
    } > VIRT AT> PHYS

    /* -------------------------------------------------------
     * .user_binaries — embedded user-program images. See
     * kernel/arch/arm/proc/user_binaries.S for the .incbin
     * entries and process_init_all() for how they are loaded
     * into per-process user PA slots.
     * ------------------------------------------------------- */
    .user_binaries :
    {
        . = ALIGN(4);
        KEEP(*(.user_binaries))
        . = ALIGN(4);
    } > VIRT AT> PHYS

    /* -------------------------------------------------------
     * .data — initialised data
     * ------------------------------------------------------- */
    .data :
    {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > VIRT AT> PHYS

    /* -------------------------------------------------------
     * .bss — zero-initialised data
     *   NOLOAD: occupies PA space but not in ELF file.
     *   Zeroed manually in start.S before bl kmain.
     * ------------------------------------------------------- */
    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _bss_start = .;
        . = ALIGN(16384);    /* boot_pgd must be 16 KB aligned (TTBR0) */
        *(.bss.pgd)          /* L1 translation table — 16 KB */
        . = ALIGN(16384);    /* per-process L1 tables — 16 KB each */
        *(.bss.proc_pgd)     /* proc_pgd[NUM_PROCESSES][4096] */
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > VIRT

    /* -------------------------------------------------------
     * .stack — per-mode exception stacks
     *   Placed after BSS, grows down within each slot.
     *   TOTAL: 512 + 1K + 1K + 1K + 8K = 11.5 KB
     * ------------------------------------------------------- */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);

        /* FIQ stack — 512 B */
        . = . + 0x200;
        PROVIDE(_fiq_stack_top = .);

        /* IRQ stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_irq_stack_top = .);

        /* Abort (prefetch + data) stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_abt_stack_top = .);

        /* Undefined instruction stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_und_stack_top = .);

        /* SVC (Supervisor) stack — 8 KB */
        . = . + 0x2000;
        PROVIDE(_svc_stack_top = .);
    } > VIRT

    PROVIDE(_heap_start = .);

    /* PA of _start — used as ELF e_entry (see comment at top).    */
    _start_phys = LOADADDR(.text);
}
