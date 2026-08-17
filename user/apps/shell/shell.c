/* ===========================================================
 * user/apps/shell/shell.c — Interactive command shell
 *
 * Six commands, each one syscall or two:
 *   help            — print this list
 *   ps              — show every process (pid, name, state)
 *   kill <pid>      — mark a process DEAD (skipped by scheduler)
 *   echo <text>     — print <text> back
 *   clear           — ANSI screen clear
 *   crash           — deliberate NULL deref (fault-isolation demo)
 *
 * Reads one char at a time via sys_read (BLOCKED until UART IRQ
 * delivers input). Line editing handles backspace and Enter.
 * =========================================================== */

#include "ulib.h"

#define LINE_MAX    64

static void prompt(void)
{
    ulib_puts("shell> ");
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Match an argv-style word at head of s. Returns pointer to the
 * first char after the matched word + trailing spaces. NULL if
 * no match. */
static const char *match_word(const char *s, const char *word)
{
    unsigned int n = ulib_strlen(word);
    if (ulib_strncmp(s, word, n) != 0)
        return (const char *)0;
    if (s[n] != '\0' && s[n] != ' ' && s[n] != '\t')
        return (const char *)0;
    return skip_spaces(s + n);
}

static void cmd_help(void)
{
    ulib_puts("commands:\n"
              "  help         — this list\n"
              "  ps           — list processes\n"
              "  kill <pid>   — kill a process\n"
              "  echo <text>  — print text\n"
              "  clear        — clear the screen\n"
              "  crash        — dereference NULL (demo)\n");
}

static void cmd_ps(void)
{
    char buf[256];
    int  n = sys_ps(buf, sizeof(buf));
    if (n > 0)
        sys_write(1, buf, (unsigned int)n);
}

static void cmd_kill(const char *arg)
{
    if (*arg == '\0') {
        ulib_puts("usage: kill <pid>\n");
        return;
    }
    int pid = ulib_atoi(arg);
    int rc  = sys_kill(pid);
    if (rc != 0) {
        ulib_puts("kill: bad pid\n");
    }
}

static void cmd_echo(const char *arg)
{
    ulib_puts(arg);
    ulib_putc('\n');
}

static void cmd_clear(void)
{
    /* ANSI: clear screen + move cursor home. */
    ulib_puts("\x1b[2J\x1b[H");
}

static void cmd_crash(void)
{
    ulib_puts("crash: about to NULL-deref — kernel should survive\n");
    *((volatile unsigned int *)0) = 0xDEADBEEFU;
}

static void dispatch(char *line)
{
    const char *p = skip_spaces(line);
    const char *rest;

    if (*p == '\0')                         { return; }
    if ((rest = match_word(p, "help")))     { cmd_help();       return; }
    if ((rest = match_word(p, "ps")))       { cmd_ps();         return; }
    if ((rest = match_word(p, "kill")))     { cmd_kill(rest);   return; }
    if ((rest = match_word(p, "echo")))     { cmd_echo(rest);   return; }
    if ((rest = match_word(p, "clear")))    { cmd_clear();      return; }
    if ((rest = match_word(p, "crash")))    { cmd_crash();      return; }

    ulib_puts("unknown command — try 'help'\n");
}

int main(void)
{
    ulib_puts("\nCortex-A-OS shell — type 'help' for commands\n");

    char line[LINE_MAX];
    unsigned int n = 0;

    prompt();

    for (;;) {
        char c;
        if (sys_read(0, &c, 1) <= 0)
            continue;

        /* Enter (CR or LF) — dispatch. */
        if (c == '\r' || c == '\n') {
            ulib_puts("\r\n");
            line[n] = '\0';
            dispatch(line);
            n = 0;
            prompt();
            continue;
        }

        /* Backspace (BS 0x08 or DEL 0x7F). */
        if (c == 0x08 || c == 0x7F) {
            if (n > 0) {
                n--;
                /* Erase-one on the terminal: back, space, back. */
                ulib_puts("\b \b");
            }
            continue;
        }

        /* Printable: append + echo. Drop anything past the line
         * buffer to avoid overflow. */
        if (n + 1 < LINE_MAX) {
            line[n++] = c;
            sys_write(1, &c, 1);
        }
    }

    return 0;   /* unreachable */
}

/* ---------------------------------------------------------------
 * Second thread of the shell process — prints a heartbeat when it
 * gets the CPU.
 *
 * It must NOT call sys_read: the kernel's wait slot for UART input
 * (scheduler.c: blocked_reader) holds exactly one thread, so a
 * second reader would overwrite the first and leave it BLOCKED
 * forever. Printing only is safe.
 *
 * The delay is what keeps the prompt usable — a heartbeat on every
 * 10 ms time slice would print ~100 lines/second and bury it.
 * --------------------------------------------------------------- */
#define HEARTBEAT_TICKS  500U   /* 500 × 10 ms = 5 s */

void thread_main(unsigned int idx)
{
    for (;;) {
        ulib_delay_ticks(HEARTBEAT_TICKS);

        ulib_puts("\r\n");
        ulib_tag_tid(idx);
        ulib_puts("heartbeat — shell worker thread alive\r\n");
        prompt();
    }
}
