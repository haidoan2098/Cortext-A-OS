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
 * UART input wait queue
 *
 * Any number of threads may sit in sys_read waiting for a byte,
 * so the waiters live in a FIFO ring rather than a single slot.
 * FIFO order is the point: the thread that blocked first gets the
 * first byte that arrives, which is both fair and observable.
 *
 * Capacity NUM_THREADS is provably enough — waitq_push refuses to
 * enqueue a thread that is already queued, so the ring can never
 * hold more entries than there are threads.
 *
 * No locking anywhere in here. The whole block/wake path runs with
 * CPSR.I = 1: ARMv7-A masks IRQ on SVC entry, and the UART IRQ
 * handler that calls scheduler_wake_reader() is itself an
 * exception. So "check the ring is empty, enqueue, sleep" is
 * atomic by construction and the classic lost-wakeup race cannot
 * happen here.
 *
 * wake_hint: just-woken thread to prefer on the next schedule()
 * so I/O-bound readers aren't stuck behind a CPU hog under plain
 * round-robin. Consumed once, then fall back to the ring walk.
 * ----------------------------------------------------------- */
#define WAITQ_CAP   NUM_THREADS

static thread_t *uart_waitq[WAITQ_CAP];
static uint32_t  waitq_head;        /* index of the next thread to wake */
static uint32_t  waitq_count;       /* entries currently queued         */

static thread_t *wake_hint;

/* Append t to the tail. Ignores a thread that is already queued —
 * see the re-entry note in scheduler_block_on_input(). */
static void waitq_push(thread_t *t)
{
    for (uint32_t i = 0; i < waitq_count; i++) {
        if (uart_waitq[(waitq_head + i) % WAITQ_CAP] == t)
            return;                 /* already waiting — no double entry */
    }

    if (waitq_count >= WAITQ_CAP)
        return;                     /* unreachable given the check above */

    uart_waitq[(waitq_head + waitq_count) % WAITQ_CAP] = t;
    waitq_count++;
}

/* Remove and return the head, or NULL when the queue is empty. */
static thread_t *waitq_pop(void)
{
    if (waitq_count == 0)
        return (thread_t *)0;

    thread_t *t = uart_waitq[waitq_head];
    uart_waitq[waitq_head] = (thread_t *)0;
    waitq_head = (waitq_head + 1U) % WAITQ_CAP;
    waitq_count--;
    return t;
}

void scheduler_block_on_input(void)
{
    if (!current)
        return;

    current->state = TASK_BLOCKED;
    waitq_push(current);
    need_reschedule = 1;
    schedule();

    /* Normally returns here once the UART IRQ woke us up and the
     * scheduler switched back in; the caller then finishes its
     * sys_read loop.
     *
     * But schedule() also returns immediately, without switching,
     * when no OTHER thread is runnable — this thread is BLOCKED
     * and still on the CPU. sys_read then loops and calls back in
     * here, which is why waitq_push must reject duplicates: the
     * alternative is one thread filling the ring by itself. The
     * resulting behaviour is a busy-wait for input, acceptable
     * only because this kernel has no idle thread to fall back
     * on. */
}

void scheduler_wake_reader(void)
{
    /* Wake exactly one waiter — the byte that just arrived can
     * only satisfy a single reader, so waking the rest would have
     * them all race for it and go back to sleep.
     *
     * Threads killed while queued are still in the ring: sys_kill
     * marks them DEAD without touching any wait queue, on purpose
     * (a kill path that must know about every queue in the kernel
     * is a coupling that rots). So skip past corpses instead of
     * letting one swallow the wakeup and leave a live reader
     * asleep. */
    for (;;) {
        thread_t *t = waitq_pop();

        if (!t)
            return;                 /* nobody left waiting */
        if (t->state != TASK_BLOCKED)
            continue;               /* DEAD, or already woken — drop it */

        /* Tracing the wake order is the way to verify FIFO
         * fairness; it prints once per keystroke, so keep it out
         * of normal builds:
         *   uart_printf("[WAITQ] wake tid=%u (%u still queued)\n",
         *               t->tid, waitq_count);                     */
        t->state = TASK_READY;
        wake_hint = t;
        need_reschedule = 1;
        return;
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
