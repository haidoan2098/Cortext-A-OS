/* ===========================================================
 * kernel/arch/arm/exception/exception_handlers.c — C-level
 * exception handlers
 *
 * All fault handlers are fatal — dump registers and halt.
 * SVC handler prints the syscall number and returns.
 * IRQ handler halts (interrupts not enabled yet).
 *
 * Dependencies: uart driver, exception.h
 * =========================================================== */

#include "drivers/intc.h"
#include "drivers/uart.h"
#include "exception.h"
#include "platform.h"
#include "proc.h"
#include "scheduler.h"
#include "syscall.h"

/* ARM mode names indexed by CPSR[4:0] */
static const char *mode_name(uint32_t cpsr)
{
    switch (cpsr & 0x1FU) {
    case 0x10: return "USR";
    case 0x11: return "FIQ";
    case 0x12: return "IRQ";
    case 0x13: return "SVC";
    case 0x17: return "ABT";
    case 0x1B: return "UND";
    case 0x1F: return "SYS";
    default:   return "???";
    }
}

/* Read Data Fault Status Register (DFSR) — cp15 c5 c0 0 */
static uint32_t read_dfsr(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(val));
    return val;
}

/* Read Data Fault Address Register (DFAR) — cp15 c6 c0 0 */
static uint32_t read_dfar(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(val));
    return val;
}

/* Read Instruction Fault Status Register (IFSR) — cp15 c5 c0 1 */
static uint32_t read_ifsr(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(val));
    return val;
}

/* Read Instruction Fault Address Register (IFAR) — cp15 c6 c0 2 */
static uint32_t read_ifar(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(val));
    return val;
}

/* -----------------------------------------------------------
 * Dump full register context
 * ----------------------------------------------------------- */
static void dump_context(const exception_context_t *ctx)
{
    uart_printf("  SPSR = 0x%08x  (mode=%s, IRQ=%s, FIQ=%s)\n",
                ctx->spsr,
                mode_name(ctx->spsr),
                (ctx->spsr & (1U << 7)) ? "masked" : "on",
                (ctx->spsr & (1U << 6)) ? "masked" : "on");
    uart_printf("  PC   = 0x%08x  (faulting/return address)\n", ctx->lr);
    uart_printf("  r0 =%08x  r1 =%08x  r2 =%08x  r3 =%08x\n",
                ctx->r[0], ctx->r[1], ctx->r[2], ctx->r[3]);
    uart_printf("  r4 =%08x  r5 =%08x  r6 =%08x  r7 =%08x\n",
                ctx->r[4], ctx->r[5], ctx->r[6], ctx->r[7]);
    uart_printf("  r8 =%08x  r9 =%08x  r10=%08x  r11=%08x\n",
                ctx->r[8], ctx->r[9], ctx->r[10], ctx->r[11]);
    uart_printf("  r12=%08x\n", ctx->r[12]);
}

static void halt_forever(void)
{
    uart_printf("[PANIC] system halted.\n");
    for (;;)
        __asm__ volatile("wfi");
}

/* ===========================================================
 * User-mode fault helpers
 *
 * A fault whose saved SPSR is in USR mode came from user code:
 * the kernel survives, the offending process is retired and a
 * reschedule is forced so someone else gets the CPU. A fault
 * with any other saved mode is a kernel bug — panic outright.
 * =========================================================== */
static int fault_from_user(const exception_context_t *ctx)
{
    return (ctx->spsr & 0x1FU) == 0x10U;
}

static void user_fault_kill(const char *kind)
{
    if (current) {
        process_t *p = current->proc;

        /* Retire the WHOLE process, not just the faulting thread —
         * POSIX SIGSEGV semantics, and for a concrete reason: the
         * fault proves this address space is being used wrongly,
         * and every sibling thread shares that exact address space.
         * A thread that scribbled over a neighbour's stack before
         * finally touching an unmapped page has already corrupted
         * state the siblings depend on.
         *
         * Leaving siblings alive also produces a zombie: kill only
         * the shell's reader thread and its worker thread keeps
         * printing a prompt that nothing will ever service. */
        uart_printf("[KILL] %s killed by %s "
                    "(faulting t%u, whole process retired)\n",
                    p->name, kind, current->index);

        for (uint32_t i = 0; i < THREADS_PER_PROC; i++) {
            if (p->threads[i])
                p->threads[i]->state = TASK_DEAD;
        }
    }
    scheduler_request_resched();
    schedule();

    /* Fall through only if no other runnable thread remains. */
    uart_printf("[PANIC] no runnable thread left after user fault\n");
    halt_forever();
}

/* ===========================================================
 * Data Abort handler
 * =========================================================== */
void handle_data_abort(exception_context_t *ctx)
{
    uint32_t dfsr = read_dfsr();
    uint32_t dfar = read_dfar();

    if (fault_from_user(ctx)) {
        uart_printf("[FAULT] data abort  DFAR=0x%08x DFSR=0x%08x PC=0x%08x\n",
                    dfar, dfsr, ctx->lr);
        user_fault_kill("data abort");
        return;     /* unreachable if schedule swapped away */
    }

    uart_printf("\n[PANIC] *** DATA ABORT (kernel) ***\n");
    uart_printf("  DFAR = 0x%08x  (faulting address)\n", dfar);
    uart_printf("  DFSR = 0x%08x  (fault status)\n", dfsr);
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * Prefetch Abort handler
 * =========================================================== */
void handle_prefetch_abort(exception_context_t *ctx)
{
    uint32_t ifsr = read_ifsr();
    uint32_t ifar = read_ifar();

    if (fault_from_user(ctx)) {
        uart_printf("[FAULT] prefetch abort  IFAR=0x%08x IFSR=0x%08x\n",
                    ifar, ifsr);
        user_fault_kill("prefetch abort");
        return;
    }

    uart_printf("\n[PANIC] *** PREFETCH ABORT (kernel) ***\n");
    uart_printf("  IFAR = 0x%08x  (faulting address)\n", ifar);
    uart_printf("  IFSR = 0x%08x  (fault status)\n", ifsr);
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * Undefined Instruction handler
 * =========================================================== */
void handle_undefined(exception_context_t *ctx)
{
    if (fault_from_user(ctx)) {
        uart_printf("[FAULT] undefined instr at PC=0x%08x\n", ctx->lr);
        user_fault_kill("undefined instruction");
        return;
    }

    uart_printf("\n[PANIC] *** UNDEFINED INSTRUCTION (kernel) ***\n");
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * SVC handler — Linux-style syscall dispatch
 *
 * Convention: r7 holds the syscall number, r0..r3 the arguments,
 * r0 receives the return value. We ignore the svc #N immediate
 * itself (always 0 in Cortex-A-OS user code).
 *
 * After dispatch we call schedule() so that sys_yield / sys_exit
 * can swap context before returning to user mode.
 * =========================================================== */
void handle_svc(exception_context_t *ctx)
{
    syscall_dispatch(ctx);
    schedule();
}

/* ===========================================================
 * IRQ handler — called from exception_entry_irq after srsdb to
 * SVC stack. Defers to the INTC dispatcher in drivers/intc.
 * =========================================================== */
void handle_irq(void)
{
    irq_dispatch();
    /* Scheduler tail — if timer_irq flipped need_reschedule,
     * this swaps SP onto the next process's kernel stack before
     * exception_entry_irq's ldmfd/rfefd drain it. */
    schedule();
}

/* ===========================================================
 * exception_init — set VBAR to our vector table
 * =========================================================== */
extern uint32_t _vectors_start;

void exception_init(void)
{
    uint32_t vbar = (uint32_t)&_vectors_start;

    /* Write VBAR — cp15 c12 c0 0 */
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0" :: "r"(vbar));

    /* Instruction sync barrier — ensure VBAR is visible before
     * any exception could fire */
    __asm__ volatile("isb" ::: "memory");

    uart_printf("[EXCP] vector table installed @ 0x%08x\n", vbar);
}
