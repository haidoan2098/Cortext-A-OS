#ifndef RINGNOVA_ULIB_H
#define RINGNOVA_ULIB_H

/* ===========================================================
 * user/libc/ulib.h — Minimal string/print helpers for user
 *                     programs. No malloc, no FILE*, no errno.
 *
 * Everything builds on top of sys_write.
 * =========================================================== */

#include "syscall.h"

unsigned int ulib_strlen(const char *s);
void         ulib_puts(const char *s);     /* write(1, s, strlen(s)) */
void         ulib_putu(unsigned int v);    /* unsigned decimal       */
void         ulib_putx(unsigned int v);    /* 8-digit hex, no "0x"   */
void         ulib_putc(char c);            /* single byte            */

/* Spin until `ticks` kernel ticks (10 ms each) have elapsed,
 * yielding the CPU while waiting so siblings make progress. */
void         ulib_delay_ticks(unsigned int ticks);

/* Print "pid=N " prefix. Useful when multiple processes share
 * a terminal and you want to know which one spoke. */
void         ulib_tag(void);

/* Print "[pid N.I] " prefix — same idea, but distinguishes the
 * threads inside one process. No sys_gettid needed: crt0 hands
 * each thread its index as thread_main's argument, so the caller
 * already knows which one it is. */
void         ulib_tag_tid(unsigned int idx);

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

#endif /* RINGNOVA_ULIB_H */
