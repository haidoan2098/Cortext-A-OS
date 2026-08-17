/* ===========================================================
 * user/crt0.S — User-program entry stub
 *
 * The kernel context-switches EVERY THREAD of a process to this
 * one label (each thread's initial frame has PC = user_entry =
 * 0x40000000 = start of .text). The kernel knows nothing about
 * the program's symbols, so it cannot jump a second thread to a
 * different function. Instead it passes the thread's index in r0
 * (see thread_build_initial_frame in kernel/proc/thread.c) and
 * lets this stub decide where to go.
 *
 * SP_usr is already set per-thread by the kernel (ctx.sp_usr =
 * that thread's own stack slice); we rely on that and don't
 * re-init it here.
 * =========================================================== */

    .section .text._ustart, "ax"
    .arm
    .global _ustart

_ustart:
    /* r0 = this thread's index within its process. Branch on it
     * WITHOUT touching r0: on the thread_main path it doubles as
     * the AAPCS first argument, so any scratch use here would
     * destroy the thread's identity. cmp only writes flags. */
    cmp     r0, #0
    bne     .Lthread_entry

    /* Index 0 — the main thread. main() takes no arguments, so
     * whatever r0 holds is ignored from here on. */
    bl      main
    b       __noreturn_sys_exit

.Lthread_entry:
    /* Index != 0 — a secondary thread. r0 is already the index,
     * i.e. thread_main's first argument, so call straight in.
     * ulib supplies a weak thread_main that just exits, so a
     * program with no second thread body still links. */
    bl      thread_main
    b       __noreturn_sys_exit

    /* Safety halt — should not be reached. */
1:  b       1b

/* Tiny shim so crt0 stays assembly-only: inline asm svc. */
    .global __noreturn_sys_exit
__noreturn_sys_exit:
    mov     r7, #3              @ SYS_EXIT — must match syscall.h
    svc     #0
2:  b       2b
