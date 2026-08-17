/* ============================================================
 * kernel/drivers/uart/uart_core.c — UART subsystem core
 *
 * Owns:
 *   - Active console pointer (set by platform/<board>/board.c)
 *   - RX ring buffer (shared by every UART chip driver)
 *   - Generic formatting helpers (uart_puts, uart_printf, ...)
 *
 * Dispatches hardware-touching calls to console->ops->*. The
 * kernel core includes only <drivers/uart.h> — it never sees
 * a chip name.
 * ============================================================ */

#include <stdint.h>
#include <stdarg.h>
#include "drivers/uart.h"

/* ---- Active console ---- */
static struct uart_device *console;

void uart_set_console(struct uart_device *dev) { console = dev; }

/* ---- RX ring (single-producer IRQ / single-consumer syscall) ---- */
#define UART_RX_BUF_SIZE    128U
#define UART_RX_MASK        (UART_RX_BUF_SIZE - 1U)

static char              rx_buf[UART_RX_BUF_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;

void uart_rx_push(char c)
{
    uint32_t next = rx_head + 1U;
    if ((next & UART_RX_MASK) != (rx_tail & UART_RX_MASK)) {
        rx_buf[rx_head & UART_RX_MASK] = c;
        rx_head = next;
    }
}

int uart_rx_empty(void) { return rx_head == rx_tail; }

int uart_rx_pop(void)
{
    if (rx_head == rx_tail)
        return -1;
    int c = (unsigned char)rx_buf[rx_tail & UART_RX_MASK];
    rx_tail++;
    return c;
}

/* ---- Dispatch to chip driver ---- */
void uart_init(void)
{
    console->ops->init(console);
}

void uart_putc(char c)
{
    if (c == '\n')
        console->ops->putc(console, '\r');
    console->ops->putc(console, c);
}

void uart_rx_irq(void)
{
    console->ops->rx_irq(console);
}

/* ============================================================
 *  Formatting — sits entirely on top of uart_putc(). Chip drivers
 *  touch hardware; everything below only emits bytes via the
 *  subsystem dispatch.
 * ============================================================ */

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_print_hex(uint32_t val)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;

    uart_puts("0x");
    for (i = 28; i >= 0; i -= 4)
        uart_putc(digits[(val >> i) & 0xFU]);
}

static void uart_print_dec(uint32_t val)
{
    char buf[10];
    int i = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0)
        uart_putc(buf[i]);
}

static void uart_print_int(int32_t val)
{
    if (val < 0) {
        uart_putc('-');
        uart_print_dec((uint32_t)(-(val + 1)) + 1);
    } else {
        uart_print_dec((uint32_t)val);
    }
}

void uart_printf(const char *fmt, ...)
{
    static const char hex[] = "0123456789abcdef";
    va_list ap;
    uint32_t uval;
    int i;

    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            uart_putc(*fmt++);
            continue;
        }
        fmt++;

        /* %-Ns — string left-aligned in a field of N (single digit).
         * Only reason it exists: keeping the process-name column in
         * the boot log and `ps` listing straight. */
        if (fmt[0] == '-' && fmt[1] >= '1' && fmt[1] <= '9'
            && fmt[2] == 's') {
            const char *s = va_arg(ap, const char *);
            uint32_t    w = (uint32_t)(fmt[1] - '0');
            uint32_t    l = 0;

            if (!s)
                s = "(null)";
            while (s[l]) {
                uart_putc(s[l]);
                l++;
            }
            while (l < w) {
                uart_putc(' ');
                l++;
            }
            fmt += 3;
            continue;
        }

        if (fmt[0] == '0' && fmt[1] == '8' && fmt[2] == 'x') {
            uval = va_arg(ap, uint32_t);
            for (i = 28; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            fmt += 3;
            continue;
        }

        switch (*fmt) {
        case 'c':
            uart_putc((char)va_arg(ap, int));
            break;
        case 's':
            uart_puts(va_arg(ap, const char *));
            break;
        case 'd':
            uart_print_int(va_arg(ap, int32_t));
            break;
        case 'u':
            uart_print_dec(va_arg(ap, uint32_t));
            break;
        case 'x':
            uval = va_arg(ap, uint32_t);
            for (i = 28; i >= 0; i -= 4) {
                if ((uval >> i) & 0xFU || i == 0)
                    break;
            }
            for (; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            break;
        case 'p':
            uval = va_arg(ap, uint32_t);
            uart_puts("0x");
            for (i = 28; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            break;
        case '%':
            uart_putc('%');
            break;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
