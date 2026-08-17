#ifndef CORTEX_A_OS_ULIB_H
#define CORTEX_A_OS_ULIB_H

/* ===========================================================
 * user/libc/ulib.h — Minimal string/print helpers for user
 *                     programs. No malloc, no FILE*, no errno.
 *
 * Everything builds on top of sys_write.
 * =========================================================== */

#include "syscall.h"

/* ulib_printf — format one message into a local buffer and emit it
 * with a SINGLE sys_write.
 *
 * That single call is what keeps output readable when several
 * threads share the UART: a syscall runs with CPSR.I = 1, so it
 * cannot be preempted, and one write therefore cannot be torn
 * apart. Building a line out of many ulib_putc calls does get torn
 * — one sys_write per character, and a sibling thread can land
 * between any two of them.
 *
 * This is NOT a lock. Concurrent threads still interleave freely
 * BETWEEN messages; the guarantee is only that each message
 * arrives whole. Lines longer than the internal buffer are
 * truncated rather than split.
 *
 * Conversions: %u %x %p %s %c %%
 */
void         ulib_printf(const char *fmt, ...);

unsigned int ulib_strlen(const char *s);
void         ulib_puts(const char *s);     /* write(1, s, strlen(s)) */
void         ulib_putu(unsigned int v);    /* unsigned decimal       */
void         ulib_putx(unsigned int v);    /* 8-digit hex, no "0x"   */
void         ulib_putc(char c);            /* single byte            */

/* Spin until `ticks` kernel ticks (10 ms each) have elapsed,
 * yielding the CPU while waiting so siblings make progress. */
void         ulib_delay_ticks(unsigned int ticks);

/* Log-line identity convention: every message starts with
 * "[<program> t<index>] ", matching what the kernel's boot dump,
 * `ps`, and the [KILL]/[EXIT] lines print. Programs write the
 * prefix as a literal in their format string — a program knows
 * its own name, so there is no runtime lookup and no name-width
 * guessing. Pad the name to 7 columns to keep logs aligned.
 *
 * Example:  ulib_printf("[counter t0] writes count=%u\n", n);
 */

/* Entry point for every non-main thread of a process. crt0 calls
 * this with the thread's index (1, 2, ...) when the kernel starts
 * a thread whose index is nonzero.
 *
 * ulib provides a weak do-nothing implementation that exits the
 * thread immediately, so programs with no use for a second thread
 * need not define anything. Override it by simply defining
 * thread_main() in the program. */
void         thread_main(unsigned int idx);

/* Tiny string helpers for shell parsing. */
int          ulib_strcmp(const char *a, const char *b);
int          ulib_strncmp(const char *a, const char *b, unsigned int n);
int          ulib_atoi(const char *s);    /* stops at first non-digit */

#endif /* CORTEX_A_OS_ULIB_H */
