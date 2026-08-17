/* ============================================================
 * kernel/sched/scheduler.c — Round-robin preemptive scheduler
 *
 * The scheduling unit is the THREAD, not the process. One time
 * slice = one timer tick (10 ms). scheduler_tick() flips a flag
 * from the IRQ context; schedule() at the tail of handle_irq
 * walks the TCB ring and swaps to the next runnable thread via
 * context_switch(). Threads marked BLOCKED or DEAD are skipped.
 *
 * Because the ring walks threads, two threads of one process are
 * scheduled independently — and context_switch() notices they
 * share an address space.
 *
 * Dependencies: thread.h (TCB + context_switch), proc.h (names
 *               for the debug log), uart
 * ============================================================ */

#include <stdint.h>
#include "drivers/uart.h"
#include "platform.h"
#include "proc.h"
#include "scheduler.h"
#include "thread.h"

/* Raised by the timer IRQ, cleared by schedule(). No locking
 * needed — single-core, accessed only from SVC mode with IRQ
 * currently masked by the exception entry. */
static volatile int need_reschedule;

/* Count switches so the boot log can confirm round-robin is
 * cycling without spamming one line per tick. */
static uint32_t switch_count;

void scheduler_tick(void)
{
    need_reschedule = 1;
}

/* Used by syscall handlers (sys_yield, sys_exit) to ask for an
 * immediate switch when schedule() runs at handle_svc's tail. */
void scheduler_request_resched(void)
{
    need_reschedule = 1;
}

/* -----------------------------------------------------------
 * BLOCKED state — only one thread can sleep on UART input at
 * a time (the shell's main thread). One pointer slot is enough;
 * multi-reader support would require a wait queue.
 *
 * wake_hint: just-woken thread to prefer on the next schedule()
 * so I/O-bound readers aren't stuck behind a CPU hog under plain
 * round-robin. Consumed once, then fall back to the ring walk.
 * ----------------------------------------------------------- */
static thread_t *blocked_reader;
static thread_t *wake_hint;

void scheduler_block_on_input(void)
{
    if (!current)
        return;

    current->state = TASK_BLOCKED;
    blocked_reader = current;
    need_reschedule = 1;
    schedule();
    /* Returns here once UART IRQ woke us up and the scheduler
     * switched back in. Caller then finishes the sys_read loop. */
}

void scheduler_wake_reader(void)
{
    thread_t *t = blocked_reader;
    if (t && t->state == TASK_BLOCKED) {
        t->state = TASK_READY;
        blocked_reader = (thread_t *)0;
        wake_hint = t;
        need_reschedule = 1;
    }
}

static void perform_switch(thread_t *prev, thread_t *cand)
{
    switch_count++;

    /* Only demote still-running threads back to READY — do
     * not resurrect a DEAD or BLOCKED prev. */
    if (prev->state == TASK_RUNNING)
        prev->state = TASK_READY;
    cand->state = TASK_RUNNING;
    current = cand;

    context_switch(prev, cand);
}

void schedule(void)
{
    if (!need_reschedule || !current)
        return;
    need_reschedule = 0;

    thread_t *prev = current;

    /* Wake-up preemption: a thread that just came out of BLOCKED
     * gets the CPU ahead of the round-robin sweep. Keeps interactive
     * readers snappy when CPU-bound siblings are also READY. */
    thread_t *hint = wake_hint;
    wake_hint = (thread_t *)0;
    if (hint && hint != prev
        && (hint->state == TASK_READY || hint->state == TASK_RUNNING)) {
        perform_switch(prev, hint);
        return;
    }

    uint32_t start = (prev->tid + 1U) % NUM_THREADS;

    for (uint32_t i = 0; i < NUM_THREADS; i++) {
        uint32_t idx = (start + i) % NUM_THREADS;
        thread_t *cand = &threads[idx];

        if (cand == prev)
            continue;
        if (cand->state != TASK_READY && cand->state != TASK_RUNNING)
            continue;

        perform_switch(prev, cand);
        return;
    }

    /* No other runnable thread — keep the current one. */
}
