/* ===========================================================
 * user/apps/counter/counter.c — User process #0 (2 threads)
 *
 * Demonstrates the defining property of a thread: both threads
 * of this process share one address space, so they share this
 * file's globals.
 *
 *   thread 0 (main)        — owns shared_count, increments it
 *   thread 1 (thread_main) — only reads shared_count
 *
 * Single writer / single reader on purpose: no locking is needed,
 * so the output stays clean and the only thing it proves is that
 * the sharing works. Each thread also prints the address of one
 * of its own locals, which lands in a different 64 KB slice —
 * shared globals, private stacks.
 * =========================================================== */

#include "ulib.h"

/* ~3 s per print regardless of platform clock speed. Yield while
 * waiting so other threads get CPU during the delay. */
#define DELAY_TICKS     300U    /* 300 × 10 ms = 3 s */
#define WATCH_TICKS     500U    /* 500 × 10 ms = 5 s */

/* In .bss — one instance per PROCESS, shared by every thread of
 * that process. `volatile` because thread 1 must re-read it from
 * memory rather than caching it in a register across yields. */
static volatile unsigned int shared_count;

int main(void)
{
    unsigned int prev = sys_ticks();

    for (;;) {
        unsigned int now = sys_ticks();
        unsigned int dt  = now - prev;                 /* 10 ms units */
        unsigned int mark;                             /* stack probe */
        prev = now;

        /* One printf = one sys_write = one untearable line, even
         * with the sibling thread printing concurrently.
         *
         * The prefix is a literal: this program knows its own name,
         * so there is no reason to spend a sys_getpid on every line
         * and no name-width guessing at runtime. */
        ulib_printf("[counter t0] writes count=%u  dt=%ums  &mark=%p\n",
                    shared_count++, dt * 10U, (void *)&mark);

        ulib_delay_ticks(DELAY_TICKS);
    }

    return 0;   /* unreachable */
}

/* Reader thread. Never writes shared_count — it just watches the
 * value thread 0 is producing. If thread 1 printed a value that
 * never advanced, the two threads would not really be sharing an
 * address space. */
void thread_main(unsigned int idx)
{
    for (;;) {
        unsigned int mark;                             /* stack probe */

        ulib_printf("[counter t%u] reads  count=%u"
                    "             &mark=%p\n",
                    idx, shared_count, (void *)&mark);

        ulib_delay_ticks(WATCH_TICKS);
    }
}
