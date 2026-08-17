/* ===========================================================
 * user/apps/runaway/runaway.c — User process #1 (silent hogs)
 *
 * Both threads run an unbounded busy loop and neither ever calls
 * sys_yield. The kernel's 10 ms timer IRQ must still preempt
 * them, otherwise counter and shell would starve.
 *
 * With two threads this proves something the single-threaded
 * version could not: preemption works BETWEEN THREADS OF ONE
 * PROCESS, not merely between address spaces. Verification is
 * `ps` — both runaway rows stay READY / RUNNING no matter how
 * long they spin, while other processes keep printing.
 *
 * Deliberately silent after the startup banner: interactive demos
 * need a clean prompt, and chatty hogs would bury shell output.
 * =========================================================== */

#include "ulib.h"

/* One spin quantum. Long enough that the 10 ms timer IRQ is
 * guaranteed to land inside it several times over. */
static void spin_forever(void)
{
    for (;;) {
        volatile unsigned int i;
        for (i = 0; i < 1000000U; i++)
            __asm__ volatile("" ::: "memory");
        /* still no sys_yield — preemption is the scheduler's job */
    }
}

int main(void)
{
    ulib_tag_tid(0);
    ulib_puts("runaway thread started — silent, no sys_yield\n");

    spin_forever();

    return 0;   /* unreachable */
}

void thread_main(unsigned int idx)
{
    ulib_tag_tid(idx);
    ulib_puts("runaway thread started — silent, no sys_yield\n");

    spin_forever();
}
