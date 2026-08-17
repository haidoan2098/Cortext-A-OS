#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

/* ============================================================
 * thread.h — Thread Control Block and thread management API
 *
 * A thread is the unit of *execution*; a process (proc.h) is the
 * unit of *address space*. The scheduler schedules threads, not
 * processes. Every thread carries:
 *   - its own saved CPU context
 *   - its own 8 KB kernel stack (each thread can trap into the
 *     kernel and block there independently — kernel stacks are
 *     never shared, even between threads of one process)
 *   - its own user stack slice inside the owning process's 1 MB
 *     user window
 * and borrows, via ->proc, the page table / user image / pid of
 * the process it belongs to.
 *
 * Dependencies: platform.h (NUM_THREADS, KSTACK_SIZE)
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "platform.h"

struct process;

typedef enum {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

/* Saved CPU context. The actual register frame consumed on
 * resume lives on the thread's own kernel stack (built by
 * thread_build_initial_frame); this struct parks the banked
 * registers and pointer state that cannot live there. */
typedef struct {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11, r12;
    uint32_t sp_svc;        /* kernel SP — points at saved frame */
    uint32_t lr_svc;
    uint32_t spsr;          /* CPSR to restore on return to user  */
    uint32_t sp_usr;        /* banked USR mode SP                 */
    uint32_t lr_usr;
} cpu_context_t;

typedef struct thread {
    cpu_context_t    ctx;           /* offset 0 — assembly-friendly */

    struct process  *proc;          /* address space owner          */
    uint32_t         tid;           /* global index into threads[]  */
    uint32_t         index;         /* index within proc; 0 = main  */
    task_state_t     state;

    void            *kstack_base;   /* low addr of the 8 KB region  */
    uint32_t         kstack_size;

    uint32_t         user_stack_top; /* proc's stack top - index*SZ */
} thread_t;

/* Public table + cursor — populated by thread_init_all().
 * `current` is the running THREAD, not the running process;
 * reach the process through current->proc. */
extern thread_t  threads[NUM_THREADS];
extern thread_t *current;

/* Build all NUM_THREADS TCBs. Must run after process_init_all()
 * because each thread needs its owning process's user_entry and
 * user window already set up. Sets current = &threads[0]. */
void thread_init_all(void);

/* Pretty-print one TCB over UART. Debug only. */
void thread_dump(const thread_t *t);

/* context_switch — implemented in kernel/arch/arm/proc/context_switch.S.
 *
 * Saves prev's kernel state onto prev's SVC stack (callee-saved
 * GPRs + lr + banked SP_usr/LR_usr) and loads the equivalent for
 * next, then returns via bx lr — landing in whatever kernel code
 * next was last executing when it yielded the CPU.
 *
 * The address space is reloaded from next->proc->pgd_pa. Two
 * threads of the same process therefore share a page table; see
 * the assembly for how that is (or is not yet) exploited.
 *
 * prev == NULL means "first-time entry" — no save side; caller
 * (kmain) never needs to return. next's initial kernel stack is
 * pre-built so the bx lr at the epilogue jumps to
 * ret_from_first_entry, which pops the 16-word IRQ-exit frame
 * and enters USR mode. */
void context_switch(struct thread *prev, struct thread *next);

/* Bootstrap helper: kick off the first thread from kmain. Never
 * returns. Equivalent to context_switch(NULL, &threads[0]). */
static inline void __attribute__((noreturn))
thread_first_run(struct thread *first)
{
    context_switch((void *)0, first);
    for (;;) { /* unreachable */ }
}

/* Struct offsets used by context_switch.S — must stay in sync.
 * _Static_assert below guards accidental layout drift.
 * PROC_PGD_PA_OFFSET lives in proc.h since it indexes process_t. */
#define CTX_SP_SVC_OFFSET       52
#define CTX_LR_SVC_OFFSET       56
#define CTX_SPSR_OFFSET         60
#define CTX_SP_USR_OFFSET       64
#define CTX_LR_USR_OFFSET       68
#define THREAD_PROC_OFFSET      72

_Static_assert(offsetof(cpu_context_t, sp_svc) == CTX_SP_SVC_OFFSET,
               "ctx.sp_svc offset drifted");
_Static_assert(offsetof(cpu_context_t, sp_usr) == CTX_SP_USR_OFFSET,
               "ctx.sp_usr offset drifted");
_Static_assert(offsetof(cpu_context_t, lr_usr) == CTX_LR_USR_OFFSET,
               "ctx.lr_usr offset drifted");
_Static_assert(offsetof(thread_t, proc) == THREAD_PROC_OFFSET,
               "thread.proc offset drifted");

#endif /* KERNEL_THREAD_H */
