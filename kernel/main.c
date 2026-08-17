/* ============================================================
 * kernel/main.c — Kernel C entry point
 *
 * kmain() runs at high VA after start.S has enabled the MMU
 * and trampolined PC into the 0xC0... alias. It brings up
 * UART, exception vectors, IRQ + timer, the 3 static processes,
 * then drops into the first user process via process_first_run.
 * ============================================================ */

#include "board.h"
#include "drivers/intc.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "exception.h"
#include "mmu.h"
#include "platform.h"
#include "proc.h"
#include "scheduler.h"

/* Linker-provided symbols */
extern uint32_t _text_start, _text_end;
extern uint32_t _data_start, _data_end;
extern uint32_t _bss_start, _bss_end;
extern uint32_t _svc_stack_top;

static uint32_t read_cpsr(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cpsr" : "=r"(val));
    return val;
}

/*
 * kmain() — kernel C entry point.
 *
 * By the time we get here:
 *   - start.S has computed PHYS_OFFSET and called mmu_init(),
 *     which built boot_pgd at PA and turned the MMU on.
 *   - start.S has trampolined PC into the high-VA alias via
 *     `ldr pc, =_start_va` and re-seated every mode's SP at VA.
 *
 * So UART, exception init, drivers, processes — everything —
 * run with the MMU on and addresses resolving through it.
 */
void kmain(void)
{
    uint32_t cpsr;

    /* Bind chip drivers to board addresses before any subsystem
     * call — uart_init() dispatches via console->ops, so console
     * must be set first. */
    platform_init_devices();

    uart_init();

    uart_printf("\n");
    uart_printf("================================================\n");
    uart_printf("  Cortex-A-OS — ARMv7-A bare-metal kernel\n");
    uart_printf("  Built %s %s\n", __DATE__, __TIME__);
    uart_printf("================================================\n");

    uart_printf("[UART] init done @ %p\n", UART0_BASE);
    uart_printf("[BOOT] platform : %s\n", PLATFORM_NAME);
    uart_printf("[BOOT] .text    : %p — %p\n",
                (uint32_t)&_text_start, (uint32_t)&_text_end);
    uart_printf("[BOOT] .data    : %p — %p\n",
                (uint32_t)&_data_start, (uint32_t)&_data_end);
    uart_printf("[BOOT] .bss     : %p — %p\n",
                (uint32_t)&_bss_start, (uint32_t)&_bss_end);
    uart_printf("[BOOT] SVC stack: %p\n", (uint32_t)&_svc_stack_top);
    uart_printf("[BOOT] &kmain   : %p  (expect 0xC01xxxxx)\n",
                (uint32_t)&kmain);

    cpsr = read_cpsr();
    uart_printf("[BOOT] CPSR     : %p (mode=%x, IRQ=%s, FIQ=%s)\n",
                cpsr,
                cpsr & 0x1FU,
                (cpsr & (1U << 7)) ? "masked" : "on",
                (cpsr & (1U << 6)) ? "masked" : "on");

    mmu_print_status();

    exception_init();

    irq_init();
    timer_init(10000);                      /* 10 ms tick */
    irq_register(IRQ_TIMER, timer_irq);
    irq_enable(IRQ_TIMER);
    irq_register(IRQ_UART0, uart_rx_irq);   /* wakes sys_read */
    irq_enable(IRQ_UART0);

    /* Address spaces first (page tables + program images), then
     * the threads that run inside them — thread_init_all() reads
     * each process's user_entry, so the order matters. */
    process_init_all();
    thread_init_all();

    /* Dump the process→thread tree once both halves exist. */
    for (uint32_t i = 0; i < NUM_PROCESSES; i++)
        process_dump(&processes[i]);

    /* Kernel has fully migrated off PA — tear down the identity
     * map so any stray PA dereference after this point faults
     * immediately. */
    mmu_drop_identity();

    /* TCBs stable, VA world tidy — arm scheduler_tick so each
     * timer IRQ flips need_reschedule. Ticks before this point
     * bumped tick_count but schedule() stayed a no-op. */
    timer_set_handler(scheduler_tick);

    /* IRQs stay CPU-masked throughout kmain. ret_from_first_entry's
     * rfefd restores USR CPSR with I=0 atomically when the first
     * process enters user mode; every exception thereafter re-enters
     * with I=1. Enabling IRQs while kmain is still running would let
     * a timer preemption save kmain's SVC state into threads[0].ctx
     * and clobber the initial frame built by thread_build_initial_frame. */
    uart_printf("[BOOT] boot complete — entering user mode pid=%u "
                "(USR @ 0x%08x)\n",
                processes[0].pid, processes[0].user_entry);

    /* Bootstrap the first thread via the shared context_switch
     * path (prev=NULL: no save side, load-only). Once IRQ-driven
     * preemption is wired up, every subsequent switch takes the
     * same route with both pointers non-null. */
    thread_first_run(&threads[0]);
    /* noreturn — code below is unreachable */
}
