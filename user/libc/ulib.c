/* ===========================================================
 * user/libc/ulib.c — User-space helper functions
 *
 * Pure user-mode code; everything routes through sys_write.
 * =========================================================== */

#include <stdarg.h>

#include "ulib.h"

/* ------------------------------------------------------------
 * ulib_printf — one message, one sys_write
 *
 * Everything is composed into a stack buffer first. The buffer is
 * a local, so each thread formatting concurrently gets its own —
 * no shared scratch space, nothing to serialise.
 * ------------------------------------------------------------ */
#define ULIB_LINE_MAX   192

/* Every emitter takes the destination's real capacity — the buffer
 * is not always ULIB_LINE_MAX (see ulib_putx), and hard-coding that
 * constant here would overflow the smaller callers. */
static void put_ch(char *buf, unsigned int *n, unsigned int cap, char c)
{
    if (*n < cap)
        buf[(*n)++] = c;
}

static void put_str(char *buf, unsigned int *n, unsigned int cap,
                    const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        put_ch(buf, n, cap, *s++);
}

static void put_dec(char *buf, unsigned int *n, unsigned int cap,
                    unsigned int v)
{
    char tmp[10];
    int  i = 0;

    if (v == 0) {
        put_ch(buf, n, cap, '0');
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (--i >= 0)
        put_ch(buf, n, cap, tmp[i]);
}

/* pad8 = always emit 8 digits (addresses); otherwise trim leading
 * zeroes down to a single digit minimum. */
static void put_hex(char *buf, unsigned int *n, unsigned int cap,
                    unsigned int v, int pad8)
{
    static const char digit[] = "0123456789abcdef";
    int started = 0;

    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned int nib = (v >> shift) & 0xFU;
        if (pad8 || nib || started || shift == 0) {
            put_ch(buf, n, cap, digit[nib]);
            started = 1;
        }
    }
}

void ulib_printf(const char *fmt, ...)
{
    char         buf[ULIB_LINE_MAX];
    unsigned int n = 0;
    va_list      ap;

    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            put_ch(buf, &n, ULIB_LINE_MAX, *p);
            continue;
        }

        p++;
        switch (*p) {
        case 'u':
            put_dec(buf, &n, ULIB_LINE_MAX, va_arg(ap, unsigned int));
            break;
        case 'x':
            put_hex(buf, &n, ULIB_LINE_MAX, va_arg(ap, unsigned int), 0);
            break;
        case 'p': {
            const void *q = va_arg(ap, const void *);
            put_str(buf, &n, ULIB_LINE_MAX, "0x");
            put_hex(buf, &n, ULIB_LINE_MAX,
                    (unsigned int)(unsigned long)q, 1);
            break;
        }
        case 's':
            put_str(buf, &n, ULIB_LINE_MAX, va_arg(ap, const char *));
            break;
        case 'c':
            put_ch(buf, &n, ULIB_LINE_MAX, (char)va_arg(ap, int));
            break;
        case '%':
            put_ch(buf, &n, ULIB_LINE_MAX, '%');
            break;
        case '\0':
            p--;                    /* trailing '%' — stop cleanly */
            break;
        default:
            put_ch(buf, &n, ULIB_LINE_MAX, '%');
            put_ch(buf, &n, ULIB_LINE_MAX, *p);
            break;
        }
    }
    va_end(ap);

    if (n > 0)
        sys_write(1, buf, n);       /* the one atomic emit */
}

unsigned int ulib_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

void ulib_putc(char c)
{
    sys_write(1, &c, 1);
}

void ulib_puts(const char *s)
{
    sys_write(1, s, ulib_strlen(s));
}

void ulib_putu(unsigned int v)
{
    char buf[11];                /* enough for 32-bit unsigned */
    int  i = 0;

    if (v == 0) {
        ulib_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    /* buf now holds digits least-significant first — flip */
    while (--i >= 0)
        ulib_putc(buf[i]);
}

void ulib_putx(unsigned int v)
{
    char         buf[8];
    unsigned int n = 0;

    /* Route through the buffered path so 8 digits leave as one
     * write instead of eight interruptible ones. */
    put_hex(buf, &n, sizeof(buf), v, 1);
    sys_write(1, buf, n);
}

void ulib_delay_ticks(unsigned int ticks)
{
    unsigned int start = sys_ticks();
    while ((sys_ticks() - start) < ticks)
        sys_yield();
}

/* Weak default thread body — a program that defines its own
 * thread_main() overrides this at link time. Retiring the thread
 * immediately keeps unused thread slots out of the run queue;
 * `ps` shows them as DEAD. */
__attribute__((weak)) void thread_main(unsigned int idx)
{
    (void)idx;
    sys_exit();
}

int ulib_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int ulib_strncmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

int ulib_atoi(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}
