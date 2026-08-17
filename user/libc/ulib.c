/* ===========================================================
 * user/libc/ulib.c — User-space helper functions
 *
 * Pure user-mode code; everything routes through sys_write.
 * =========================================================== */

#include "ulib.h"

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
    /* .rodata — read-only, so threads sharing this address space
     * can use it concurrently without any coordination. */
    static const char hexdig[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4)
        ulib_putc(hexdig[(v >> shift) & 0xFU]);
}

void ulib_delay_ticks(unsigned int ticks)
{
    unsigned int start = sys_ticks();
    while ((sys_ticks() - start) < ticks)
        sys_yield();
}

void ulib_tag(void)
{
    ulib_puts("[pid ");
    ulib_putu((unsigned int)sys_getpid());
    ulib_puts("] ");
}

void ulib_tag_tid(unsigned int idx)
{
    ulib_puts("[pid ");
    ulib_putu((unsigned int)sys_getpid());
    ulib_putc('.');
    ulib_putu(idx);
    ulib_puts("] ");
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
