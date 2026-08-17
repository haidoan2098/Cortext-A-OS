#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

/* ============================================================
 * scheduler.h — Round-robin preemptive scheduler
 *
 * scheduler_tick() is registered as the timer callback. It only
 * raises a flag; the actual context switch happens inside
 * schedule(), which is invoked at the tail of handle_irq so the
 * IRQ stack frame is already settled when we swap processes.
 * ============================================================ */

void scheduler_tick(void);              /* timer IRQ callback               */
void schedule(void);                    /* consults flag, switches if needed */
void scheduler_request_resched(void);   /* syscall paths raise flag via this */

/* BLOCKED-state helpers. Used by sys_read so a thread giving up
 * the CPU to wait for UART input is skipped by the scheduler
 * until scheduler_wake_reader() moves it back to READY.
 *
 * Waiters are held in a FIFO queue, so any number of threads may
 * block on input at once; each arriving byte wakes exactly one,
 * in the order they blocked. */
void scheduler_block_on_input(void);
void scheduler_wake_reader(void);

#endif /* KERNEL_SCHED_H */
