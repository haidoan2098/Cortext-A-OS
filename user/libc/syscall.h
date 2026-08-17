#ifndef CORTEX_A_OS_USER_SYSCALL_H
#define CORTEX_A_OS_USER_SYSCALL_H

/* ===========================================================
 * user/libc/syscall.h — User-side syscall wrappers
 *
 * ABI matches kernel/include/syscall.h:
 *   r7  = syscall number
 *   r0..r3 = arguments
 *   svc #0
 *   r0 = return value
 *
 * All wrappers are inline; each syscall compiles to a handful
 * of mov/svc instructions per call site.
 * =========================================================== */

/* Syscall numbers. MUST match kernel/include/syscall.h. */
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_EXIT        3
#define SYS_READ        4
#define SYS_PS          5
#define SYS_KILL        6
#define SYS_TICKS       7

typedef int           int32_t_;
typedef unsigned int  size_t_;

static inline int sys_write(int fd, const char *buf, unsigned int len)
{
    register int          r0 __asm__("r0") = fd;
    register const char  *r1 __asm__("r1") = buf;
    register unsigned int r2 __asm__("r2") = len;
    register int          r7 __asm__("r7") = SYS_WRITE;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7)
                     : "memory");
    return r0;
}

static inline int sys_getpid(void)
{
    register int r0 __asm__("r0");
    register int r7 __asm__("r7") = SYS_GETPID;
    __asm__ volatile("svc #0"
                     : "=r"(r0)
                     : "r"(r7));
    return r0;
}

static inline int sys_read(int fd, char *buf, unsigned int len)
{
    register int          r0 __asm__("r0") = fd;
    register char        *r1 __asm__("r1") = buf;
    register unsigned int r2 __asm__("r2") = len;
    register int          r7 __asm__("r7") = SYS_READ;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7)
                     : "memory");
    return r0;
}

static inline int sys_ps(char *buf, unsigned int size)
{
    register int          r0 __asm__("r0") = (int)(unsigned long)buf;
    register unsigned int r1 __asm__("r1") = size;
    register int          r7 __asm__("r7") = SYS_PS;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r7)
                     : "memory");
    return r0;
}

static inline int sys_kill(int pid)
{
    register int r0 __asm__("r0") = pid;
    register int r7 __asm__("r7") = SYS_KILL;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7)
                     : "memory");
    return r0;
}

static inline unsigned int sys_ticks(void)
{
    register unsigned int r0 __asm__("r0");
    register int          r7 __asm__("r7") = SYS_TICKS;
    __asm__ volatile("svc #0"
                     : "=r"(r0)
                     : "r"(r7));
    return r0;
}

static inline void sys_yield(void)
{
    register int r7 __asm__("r7") = SYS_YIELD;
    /* Kernel writes 0 into the saved r0 on return. Declare r0 as
     * an early-clobber output so gcc doesn't keep a live value
     * there across the svc. */
    register int r0 __asm__("r0");
    __asm__ volatile("svc #0"
                     : "=r"(r0)
                     : "r"(r7)
                     : "memory");
    (void)r0;
}

static inline void __attribute__((noreturn)) sys_exit(void)
{
    register int r7 __asm__("r7") = SYS_EXIT;
    __asm__ volatile("svc #0"
                     :
                     : "r"(r7));
    for (;;) { /* unreachable */ }
}

#endif /* CORTEX_A_OS_USER_SYSCALL_H */
