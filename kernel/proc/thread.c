/* ============================================================
 * kernel/proc/thread.c — TCB table + thread initialisation
 *
 * Builds NUM_THREADS static thread descriptors, THREADS_PER_PROC
 * of them per process. Each thread gets its own 8 KB kernel
 * stack carrying a pre-built initial frame, so context_switch()
 * can drop it into USR mode through exactly the same code path
 * a preempted resume uses.
 *
 * Kernel stacks are per-thread, never per-process: two threads
 * of one address space can be inside the kernel simultaneously
 * (one blocked in sys_read, one mid-syscall), and each needs its
 * own SVC stack to hold that state.
 *
 * Dependencies: proc.h (process_t), uart (debug log), platform.h
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "drivers/uart.h"
#include "platform.h"
#include "proc.h"
#include "thread.h"

/* 8 KB kernel stack per thread. 8-byte alignment is the ARM
 * EABI requirement. */
static uint8_t thread_kstack[NUM_THREADS][KSTACK_SIZE]
    __attribute__((aligned(8)));

/* Trampoline landed on by context_switch's bx lr for first-time
 * entries. Defined in kernel/arch/arm/exception/exception_entry.S.
 * Pops the 16-word IRQ-exit frame and enters USR mode via rfefd. */
extern void ret_from_first_entry(void);

/* Public TCB array + current cursor */
thread_t  threads[NUM_THREADS];
thread_t *current;

/* -----------------------------------------------------------
 * thread_build_initial_frame — pre-construct two stacked frames
 * so that context_switch(NULL|prev, t) lands the thread in USR
 * mode at `entry` using the same code path a preempted resume
 * uses.
 *
 * Stack layout (low→high; stack grows down, sp_svc points low):
 *
 *   [+0x00] r4   = 0         \
 *   [+0x04] r5   = 0          |
 *   [+0x08] r6   = 0          |
 *   [+0x0C] r7   = 0          |  9-word kernel-resume frame.
 *   [+0x10] r8   = 0          |  context_switch's epilogue does
 *   [+0x14] r9   = 0          |    ldmfd sp!, {r4-r11, lr}; bx lr
 *   [+0x18] r10  = 0          |  which pops these 9 words and
 *   [+0x1C] r11  = 0          |  transfers control to lr below.
 *   [+0x20] lr   = ret_from_first_entry  /
 *
 *   [+0x24] r0   = arg       \
 *   [+0x28] r1   = 0          |
 *     ...                     |
 *   [+0x54] r12  = 0          |  16-word IRQ-exit frame that
 *   [+0x58] svc_lr = 0        |  ret_from_first_entry drains via
 *   [+0x5C] pc   = entry      |    ldmfd sp!, {r0-r12, lr}
 *   [+0x60] cpsr = 0x10       |    rfefd sp!
 *                             /
 *
 * r0 = arg is how the kernel hands a thread its identity without
 * knowing anything about the user program's symbols: crt0 reads
 * it and dispatches to main() or to the thread body.
 *
 * sp_svc = start of the 9-word kernel-resume frame. Total 25
 * words (100 bytes) reserved at the top of the 8 KB kstack.
 * ----------------------------------------------------------- */
#define KERNEL_RESUME_WORDS  9U     /* r4-r11 + lr */
#define USER_EXIT_WORDS      16U    /* r0-r12 + svc_lr + pc + cpsr */
#define INIT_STACK_WORDS     (KERNEL_RESUME_WORDS + USER_EXIT_WORDS)

static void thread_build_initial_frame(thread_t *t, uint32_t entry,
                                       uint32_t arg)
{
    uint32_t top = (uint32_t)t->kstack_base + t->kstack_size;
    uint32_t *frame = (uint32_t *)(top - INIT_STACK_WORDS * 4U);

    /* Kernel-resume frame (indices 0..8). */
    for (uint32_t i = 0; i < 8; i++)
        frame[i] = 0;                              /* r4..r11 */
    frame[8] = (uint32_t)&ret_from_first_entry;    /* lr */

    /* IRQ-exit frame (indices 9..24). */
    for (uint32_t i = 0; i < 13; i++)
        frame[9 + i] = 0;                          /* r0..r12 */
    frame[9]  = arg;                               /* r0 — thread index */
    frame[22] = 0;                                 /* svc_lr placeholder */
    frame[23] = entry;                             /* pc */
    frame[24] = 0x10U;                             /* cpsr — USR, I=0 F=0 */

    /* ctx fields consumed by context_switch.S. */
    t->ctx.sp_svc = (uint32_t)frame;
    t->ctx.lr_svc = 0;
    t->ctx.spsr   = 0x10U;          /* mirror of frame[24] — documentary */
    t->ctx.sp_usr = t->user_stack_top;
    t->ctx.lr_usr = 0;
}

/* -----------------------------------------------------------
 * thread_init_all — build all NUM_THREADS TCBs
 *
 * Threads are laid out grouped by process:
 *   tid 0..THREADS_PER_PROC-1   → processes[0]
 *   tid THREADS_PER_PROC..      → processes[1]
 *   ...
 * so tid / THREADS_PER_PROC is the pid and tid % THREADS_PER_PROC
 * is the index within the process.
 * ----------------------------------------------------------- */
void thread_init_all(void)
{
    for (uint32_t tid = 0; tid < NUM_THREADS; tid++) {
        thread_t  *t     = &threads[tid];
        uint32_t   pid   = tid / THREADS_PER_PROC;
        uint32_t   index = tid % THREADS_PER_PROC;
        process_t *p     = &processes[pid];

        t->proc  = p;
        t->tid   = tid;
        t->index = index;
        t->state = TASK_READY;

        t->kstack_base = thread_kstack[tid];
        t->kstack_size = KSTACK_SIZE;

        /* Carve this thread's user stack down from the top of
         * the process's 1 MB window. Index 0 keeps the original
         * USER_STACK_TOP so the main thread is unchanged. */
        t->user_stack_top = USER_STACK_TOP - index * USER_STACK_SIZE;

        /* Every thread of a process enters at the same address —
         * crt0's _ustart — and tells itself apart via r0. */
        thread_build_initial_frame(t, p->user_entry, index);

        p->threads[index] = t;
    }

    current = &threads[0];
}

/* -----------------------------------------------------------
 * thread_dump — one-TCB debug print
 * ----------------------------------------------------------- */
void thread_dump(const thread_t *t)
{
    static const char *const state_name[] = {
        "READY", "RUNNING", "BLOCKED", "DEAD"
    };

    /* Indented under its process, and named the same way runtime
     * log lines name it: "<process> t<index>". One identifier
     * scheme everywhere — boot log, ps, kill, user output. */
    uart_printf("[THRD]   %-7s t%u  kstack=0x%08x ustack=0x%08x "
                "sp=0x%08x %s\n",
                t->proc->name, t->index,
                (uint32_t)t->kstack_base, t->user_stack_top,
                t->ctx.sp_svc, state_name[t->state]);
}
