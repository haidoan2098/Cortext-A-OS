/* ============================================================
 * kernel/proc/process.c — PCB table + address space setup
 *
 * Builds 3 static process descriptors. After process_init_all(),
 * each PCB owns a per-process L1 table and a private 1 MB user
 * PA slot loaded with the process's binary. Threads (execution
 * contexts, kernel stacks, initial frames) are built afterwards
 * by thread_init_all() in kernel/proc/thread.c.
 *
 * Memory layout:
 *   proc 0 user PA = RAM_BASE + 0x200000
 *   proc 1 user PA = RAM_BASE + 0x300000
 *   proc 2 user PA = RAM_BASE + 0x400000
 *
 * Dependencies: mmu/pgtable, uart (debug log), platform.h
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "drivers/uart.h"
#include "mmu.h"
#include "platform.h"
#include "proc.h"

/* -----------------------------------------------------------
 * Static backing storage
 *
 * proc_pgd: each process's 16 KB L1 table. The linker places
 *   the whole array at 16 KB alignment via section .bss.proc_pgd
 *   (see kernel/linker/kernel_*.ld) — so proc_pgd[i] for every i
 *   is naturally 16 KB aligned because each row is 16 KB.
 * ----------------------------------------------------------- */
static uint32_t proc_pgd[NUM_PROCESSES][PGD_ENTRIES]
    __attribute__((aligned(PGD_ALIGN)))
    __attribute__((section(".bss.proc_pgd")));

/* User-program images (embedded via .incbin in
 * kernel/arch/arm/proc/user_binaries.S). One entry per pid. */
extern uint8_t _counter_img_start[], _counter_img_end[];
extern uint8_t _runaway_img_start[], _runaway_img_end[];
extern uint8_t _shell_img_start[],   _shell_img_end[];

typedef struct {
    const char    *name;
    const uint8_t *start;
    const uint8_t *end;
} user_image_t;

static const user_image_t user_images[NUM_PROCESSES] = {
    { "counter", _counter_img_start, _counter_img_end },
    { "runaway", _runaway_img_start, _runaway_img_end },
    { "shell",   _shell_img_start,   _shell_img_end   },
};

/* Public PCB array */
process_t processes[NUM_PROCESSES];

/* -----------------------------------------------------------
 * Minimal memcpy/memset — no libc in the kernel. Only used at
 * boot by process_init_all(), so don't bother optimising.
 * ----------------------------------------------------------- */
static void kmemcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void kmemset(void *dst, uint8_t v, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++)
        d[i] = v;
}

/* -----------------------------------------------------------
 * icache_sync — make freshly-written instruction bytes visible
 * to the I-fetch path.
 *
 * ARMv7-A separates L1 D-cache and I-cache. When the kernel
 * memcpy's a user image through the data path (via the high-VA
 * alias), the bytes sit in D-cache until cleaned to the Point
 * of Unification. Without this sync, the user's first fetch can
 * return whatever RAM held before the copy (often zero). Pair
 * the clean with an I-cache invalidate and a DSB/ISB to finish.
 * ----------------------------------------------------------- */
static void icache_sync(void *va, uint32_t len)
{
    uintptr_t start = (uintptr_t)va & ~(uintptr_t)31;   /* 32-byte line */
    uintptr_t end   = (uintptr_t)va + len;

    for (uintptr_t a = start; a < end; a += 32) {
        __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" :: "r"(a));   /* DCCMVAU */
    }
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));        /* ICIALLU */
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

/* -----------------------------------------------------------
 * process_init_all — build all NUM_PROCESSES address spaces
 * ----------------------------------------------------------- */
void process_init_all(void)
{
    for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
        process_t          *p   = &processes[i];
        const user_image_t *img = &user_images[i];
        uint32_t            img_size = (uint32_t)(img->end - img->start);

        /* Clear PCB — static storage is already zero, but be explicit */
        kmemset(p, 0, sizeof(*p));

        p->pid         = i;
        p->name        = img->name;

        p->pgd         = proc_pgd[i];
        /* pgd is the VA pointer (linker symbol); pgd_pa is what
         * the MMU needs in TTBR0. Since the kernel image is linked
         * at VMA 0xC0..., proc_pgd[i]'s VA is high and its PA is
         * one PHYS_OFFSET below. */
        p->pgd_pa      = (uint32_t)proc_pgd[i] - PHYS_OFFSET;

        p->user_entry     = USER_VIRT_BASE;
        p->user_phys_base = USER_PHYS_BASE + i * USER_PHYS_STRIDE;
        p->img_size       = img_size;

        /* Copy this process's user image into its PA slot. Reach
         * the PA through the high-VA alias (PA + PHYS_OFFSET) so
         * we never touch identity mapping. Clean D-cache + flush
         * I-cache so the user's first instruction fetch sees the
         * freshly-written bytes (see icache_sync above). */
        void *user_va = (void *)(p->user_phys_base + PHYS_OFFSET);
        kmemset(user_va, 0, USER_REGION_SIZE);
        kmemcpy(user_va, img->start, img_size);
        icache_sync(user_va, img_size);

        /* Build the per-process L1 table: kernel mirror + user
         * section at 0x40000000 → p->user_phys_base */
        pgtable_build_for_proc(p->pgd, p->user_phys_base);
    }
}

/* -----------------------------------------------------------
 * process_dump — one process and the threads it owns
 *
 * Prints the [PROC] line followed immediately by that process's
 * [THRD] lines, so the ownership tree is visible. Nothing is
 * logged during init itself: threads do not exist until
 * thread_init_all() has run, and a flat "all processes then all
 * threads" dump hides exactly the relationship this design is
 * about.
 * ----------------------------------------------------------- */
void process_dump(const process_t *p)
{
    uart_printf("[PROC] %-7s pgd=0x%08x pa=0x%08x user_pa=0x%08x "
                "img=%uB\n",
                p->name, (uint32_t)p->pgd, p->pgd_pa,
                p->user_phys_base, p->img_size);

    for (uint32_t i = 0; i < THREADS_PER_PROC; i++) {
        if (p->threads[i])
            thread_dump(p->threads[i]);
    }
}
