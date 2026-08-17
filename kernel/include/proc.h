#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

/* ============================================================
 * proc.h — Process Control Block and process management API
 *
 * A process owns an ADDRESS SPACE and the resources tied to it;
 * it does not run. Execution lives in thread.h. 3 static
 * processes, each with:
 *   - its own 16 KB L1 page table (physical isolation real)
 *   - its own 1 MB user physical slot holding the program image
 *   - all sharing the user VA window 0x40000000 (different PA)
 *   - THREADS_PER_PROC threads pointing back at it
 *
 * Dependencies: thread.h (thread_t), board.h, platform.h
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "board.h"
#include "thread.h"

typedef struct process {
    uint32_t        pid;
    const char     *name;

    uint32_t       *pgd;            /* VA of this process's L1 table   */
    uint32_t        pgd_pa;         /* PA written into TTBR0 on switch */

    uint32_t        user_entry;     /* = USER_VIRT_BASE                */
    uint32_t        user_phys_base; /* per-process physical slot       */

    /* Threads belonging to this address space. Filled in by
     * thread_init_all(); threads[0] is the main thread. */
    thread_t       *threads[THREADS_PER_PROC];

    /* Bytes of program image copied into the user slot. Kept only
     * so the boot dump can report it after init has finished.
     * Appended last on purpose — PROC_PGD_PA_OFFSET must not move. */
    uint32_t        img_size;
} process_t;

/* Public table — populated by process_init_all() */
extern process_t processes[NUM_PROCESSES];

/* Construct all NUM_PROCESSES address spaces:
 *   - allocate per-process L1 table (static BSS)
 *   - copy the program image into the process's user PA slot
 *   - populate the L1 table via pgtable_build_for_proc
 * Safe to call once after mmu_init(). Threads are created
 * separately by thread_init_all(), which must run after this. */
void process_init_all(void);

/* Pretty-print one PCB over UART. Debug only. */
void process_dump(const process_t *p);

/* Offset used by context_switch.S to reach the page table base
 * through thread->proc. _Static_assert guards layout drift. */
#define PROC_PGD_PA_OFFSET      12

_Static_assert(offsetof(process_t, pgd_pa) == PROC_PGD_PA_OFFSET,
               "process.pgd_pa offset drifted");

#endif /* KERNEL_PROC_H */
