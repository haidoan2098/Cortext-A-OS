#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

/* ============================================================
 * platform.h — Shared platform constants + board wire-up hooks
 *
 * Kernel core uses this header only for:
 *   - Shared VA layout + process table sizing
 *   - platform_init_devices()   — called early in kmain, wires
 *                                 chip drivers into subsystems
 *   - platform_map_peripherals() — called from MMU setup at PA
 *
 * Driver contracts live in <drivers/uart.h>, <drivers/timer.h>,
 * <drivers/intc.h>. Kernel core that wants UART/timer/intc API
 * includes those directly.
 * ============================================================ */

#include <stdint.h>

/* ---- Shared VA layout — identical across platforms ---- */
#define KERNEL_VIRT_BASE    0xC0000000U
#define USER_VIRT_BASE      0x40000000U
#define USER_REGION_SIZE    0x00100000U                       /* 1 MB */
#define USER_STACK_TOP      (USER_VIRT_BASE + USER_REGION_SIZE)

/* ---- Process / thread tables — sized at build time ----
 *
 * NUM_PROCESSES address spaces, each running THREADS_PER_PROC
 * threads. Every thread gets its own KSTACK_SIZE kernel stack
 * and its own USER_STACK_SIZE slice carved down from the top of
 * the owning process's 1 MB user window:
 *
 *   thread index 0 → USER_STACK_TOP
 *   thread index 1 → USER_STACK_TOP - USER_STACK_SIZE
 *   ...
 *
 * The program image grows up from USER_VIRT_BASE, so
 * THREADS_PER_PROC * USER_STACK_SIZE must stay well below
 * USER_REGION_SIZE. Section-mapped 1 MB pages give no guard
 * page — a stack overflow silently corrupts the neighbour.
 */
#define NUM_PROCESSES       3U
#define THREADS_PER_PROC    2U
#define NUM_THREADS         (NUM_PROCESSES * THREADS_PER_PROC)
#define KSTACK_SIZE         8192U
#define USER_STACK_SIZE     0x00010000U                       /* 64 KB */

/* ---- Board wire-up — kernel/platform/<p>/board.c ----
 *
 * Bind chip drivers (pl011_ops, sp804_ops, ...) to concrete
 * addresses + IRQ lines, then publish the resulting devices via
 * uart_set_console / timer_set_device / intc_set_device. Called
 * once from kmain before any subsystem API is used.
 */
void platform_init_devices(void);

/* ---- Peripheral map installer — kernel/platform/<p>/periph_map.c
 *
 * Called from mmu_build_boot_pgd() at PA (MMU off). Each platform
 * implements this by calling pgtable_map_range() with literal
 * addresses — no VA pointer dereference, safe pre-MMU.
 */
void platform_map_peripherals(uint32_t *pgd);

#endif /* KERNEL_PLATFORM_H */
