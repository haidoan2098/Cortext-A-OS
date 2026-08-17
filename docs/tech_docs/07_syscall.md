# Chapter 07 — Syscall: The gateway to the kernel

<a id="english"></a>

**English** · [Tiếng Việt](#tiếng-việt)

> 3 processes are already running round-robin in USR mode, but they're playing mute. They
> can't touch the UART (registers live outside `0x40000000`–`0x40100000`), can't call any
> kernel function, don't even know their own pid. This chapter opens a single door: `svc #0`.
> User puts the syscall number in `r7`, args in `r0-r3`, CPU traps into SVC mode, kernel
> looks up a function pointer table, executes, returns the result via `r0`. The door is
> simple enough to audit — but strong enough to build an entire userland on top of.

---

## What has been built so far

Modules marked with ★ are **new in this chapter**.

```
┌──────────────────────────────────────────────────────┐
│                     User space                       │
│                                                      │
│   counter.c, runaway.c, shell.c (còn stub)           │
│             └── sys_write/getpid/yield/exit ─┐       │
│                                              │       │
│                  svc #0 (r7, r0-r3)          │       │
└──────────────────────────────────────────────┼───────┘
                                               ▼
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌──────────────────────────────────────────────────────┐
│                    Kernel (SVC mode)                 │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ handle_svc → syscall_dispatch          │       │
│   │      read ctx->r[7] + args               │       │
│   │      syscall_table[num](r0..r3)          │       │
│   │      store return → ctx->r[0]            │       │
│   │      schedule() at tail                  │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ Tier-1 syscall handlers                │       │
│   │      sys_write (UART + user ptr check)   │       │
│   │      sys_getpid                           │       │
│   │      sys_yield  (set need_reschedule)    │       │
│   │      sys_exit   (state = DEAD + resched) │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ Fault isolation in abort handlers      │       │
│   │      SPSR.mode == USR → kill current     │       │
│   │      else → panic (kernel fault)         │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   Scheduler · Context switch · IRQ · MMU · UART     │
└──────────────────────────────────────────────────────┘
```

**Flow of a single syscall:**

```mermaid
sequenceDiagram
    participant U as user (USR)
    participant V as vector table
    participant E as exception_entry_svc
    participant D as syscall_dispatch
    participant H as handler (sys_write)
    participant S as schedule

    U->>V: svc #0 (r7=SYS_WRITE, r0-r3 args)
    Note over V: CPU: LR_svc=PC+4<br/>SPSR_svc=CPSR<br/>jump VBAR+0x08
    V->>E: ldr pc, _vec_svc
    E->>E: stmfd sp!, {r0-r12, lr}<br/>mrs r0, spsr; stmfd {r0}
    E->>D: bl handle_svc → syscall_dispatch(ctx)
    D->>H: syscall_table[ctx.r[7]](ctx.r[0..3])
    H->>H: validate user ptr<br/>do the work<br/>return value
    H-->>D: long ret
    D->>D: ctx.r[0] = ret
    D-->>E: handle_svc returns
    E->>S: schedule() tail
    Note over S: may context_switch<br/>or just return
    E->>E: ldmfd {r0} → msr spsr_cxsf<br/>ldmfd {r0-r12, pc}^
    E->>U: back to USR, r0 = ret
```

---

## Principles

### The ABI is a "contract" between user and kernel

No magic. Just a convention both sides agree on. Cortex-A-OS follows the Linux ARM
EABI style:

| Register | Role |
|---|---|
| `r7` | Syscall number |
| `r0..r3` | Argument 0..3 |
| `r0` (on return) | Result (negative = error) |

User side (inline-asm wrapper):
```c
register int r0 __asm__("r0") = fd;
register const char *r1 __asm__("r1") = buf;
register unsigned int r2 __asm__("r2") = len;
register int r7 __asm__("r7") = SYS_WRITE;
__asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
return r0;
```

Kernel side (dispatch):
```c
void syscall_dispatch(exception_context_t *ctx) {
    uint32_t num = ctx->r[7];
    if (num >= NR_SYSCALLS || !syscall_table[num]) {
        ctx->r[0] = E_BADCALL;
        return;
    }
    ctx->r[0] = syscall_table[num](ctx->r[0], ctx->r[1], ctx->r[2], ctx->r[3]);
}
```

`syscall_table` is an array of function pointers indexed by syscall number. Adding a
new syscall = adding one entry.

### User pointer validation — the first rule

The kernel **never** derefs a user-supplied pointer without checking first. The pointer
may point into kernel space, into unmapped memory, or into a region with wrong
permissions. A direct deref → kernel trap → system panic because of a user mistake.

The boundary on Cortex-A-OS is simple: user space is 1 MB at `USER_VIRT_BASE = 0x40000000`.
Every valid pointer lives in `[0x40000000, 0x40100000)`.

```c
static int valid_user_ptr(const void *p, uint32_t len) {
    uint32_t addr = (uint32_t)p;
    uint32_t end  = addr + len;
    if (end < addr)                               return 0;  /* overflow */
    if (addr < USER_VIRT_BASE)                    return 0;
    if (end > USER_VIRT_BASE + USER_REGION_SIZE)  return 0;
    return 1;
}
```

This is a simplified version of Linux's `copy_from_user` / `copy_to_user`. A real
project would replace it with a copy routine that cooperates with the page-fault
handler (if the user pointer points to a page that isn't paged in, the fault handler
installs the page and the syscall retries). Cortex-A-OS has no demand paging, so a check
plus direct access is enough.

### Fault isolation: user crash ≠ kernel crash

Data Abort, Prefetch Abort, and Undefined Instruction are all caused by buggy user
code. Chapter 02's handler was fatal — print info, then halt. Now that we have
multiple processes, halting means killing the whole system because of one buggy
process. We have to distinguish:

```c
static int fault_from_user(const exception_context_t *ctx) {
    return (ctx->spsr & 0x1FU) == 0x10U;   /* mode bits == USR */
}

void handle_data_abort(exception_context_t *ctx) {
    if (fault_from_user(ctx)) {
        uart_printf("[KILL] pid=%u ...\n", current->pid);
        current->state = TASK_DEAD;
        scheduler_request_resched();
        schedule();
        /* unreachable if any other process is runnable */
    }
    /* kernel fault → panic như cũ */
    halt_forever();
}
```

Same pattern for prefetch abort and undefined instruction. The saved SPSR (pushed by
the exception entry) is the CPSR **before** the fault happened — that's the signal
for "who was running when things went wrong".

### Tail `schedule()` — yield must take effect immediately

`sys_yield` and `sys_exit` only set `need_reschedule = 1`. If nobody calls
`schedule()` after the handler returns, the flag is useless until the next timer IRQ.
Fix: `handle_svc` calls `schedule()` at its tail, just like `handle_irq`:

```c
void handle_svc(exception_context_t *ctx) {
    syscall_dispatch(ctx);
    schedule();
}
```

By the tail call, the SVC frame has settled on the kernel stack — context_switch
swaps SP safely. If `need_reschedule` isn't set (a "normal" syscall like `write` or
`getpid`), `schedule()` is an immediate no-op.

---

## Context

```
State before chapter 07:
- 3 process chạy USR mode, round-robin bởi timer IRQ (chapter 06)
- handle_svc chỉ in "[SVC] syscall #N" rồi return (chapter 03 test T5)
- Abort handlers vẫn halt_forever cho mọi fault
- user program ban đầu là asm spin loop — chưa có user logic thật
```

---

## The problem

1. **User space does nothing useful.** No I/O, doesn't know its own pid, can't even exit itself.
2. **handle_svc is only a placeholder.** Doesn't read r7, doesn't dispatch, doesn't return a result.
3. **Every user crash kills the kernel too.** No isolation — can't demonstrate real multi-process.
4. **Cooperative yield doesn't work.** Even when user calls `sys_yield`, if the flag isn't consumed at the right time, yield is half-baked.

---

## Design

### Fixed syscall numbers

```c
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_EXIT        3
```

These values cannot change — they're encoded into user binaries. Tier 2 (`read`, `ps`, `kill`)
is left to chapter 09.

### 4 Tier-1 handlers

| Handler | Signature | Main job |
|---|---|---|
| `sys_write(fd, buf, len)` | — | `valid_user_ptr` → `uart_putc` byte by byte → return `len` |
| `sys_getpid()` | — | return `current->pid` |
| `sys_yield()` | — | `scheduler_request_resched()` → return 0 |
| `sys_exit()` | noreturn | `current->state = TASK_DEAD` + resched |

`sys_write` ignores `fd` — every fd goes to UART. Anything more is unnecessary for the
current scope.

### Fault handler: kill if user, panic if kernel

Helper `user_fault_kill(kind)`:
```c
static void user_fault_kill(const char *kind) {
    if (current) {
        uart_printf("[KILL] pid=%u name=%s killed by %s\n",
                    current->pid, current->name, kind);
        current->state = TASK_DEAD;
    }
    scheduler_request_resched();
    schedule();
    /* fallback if no other runnable */
    halt_forever();
}
```

All 3 handlers (data/prefetch/undef abort) check `fault_from_user(ctx)` at the top of
the function: true → call the helper, false → keep the old panic path.

---

## How it works

### A single write

User:
```c
sys_write(1, "hello\n", 6);
```

Flow:
1. Wrapper sets `r0=1, r1=&"hello\n", r2=6, r7=0`.
2. `svc #0` → CPU: `LR_svc = PC+4`, `SPSR_svc = CPSR`, jump `VBAR + 0x08`.
3. Vector: `ldr pc, _vec_svc` → `exception_entry_svc`.
4. Entry: `stmfd sp!, {r0-r12, lr}`; `mrs r0, spsr; stmfd {r0}`; `mov r0, sp` (= ctx).
5. `bl handle_svc` — ctx points to the frame on the SVC stack.
6. `syscall_dispatch(ctx)`: reads `ctx->r[7] = 0` → `sys_write(1, buf_addr, 6, _)`.
7. `sys_write`: `valid_user_ptr(buf, 6)` passes → loop `uart_putc(buf[i])` → return 6.
8. Dispatch: `ctx->r[0] = 6`.
9. Back to `handle_svc` tail → `schedule()` (no-op because `need_reschedule==0`).
10. Entry: `ldmfd sp!, {r0}; msr spsr_cxsf, r0; ldmfd sp!, {r0-r12, pc}^`.
11. User sees `r0 = 6`.

### Crash from user

User:
```c
*(volatile int *)0 = 0xDEAD;
```

Flow:
1. CPU writes to VA 0: MMU walk of proc_pgd entry 0 = FAULT → **Data Abort**.
2. `LR_abt = PC + 8`, `SPSR_abt = CPSR (USR mode, bit[4:0] = 0x10)`, jump `VBAR + 0x10`.
3. `exception_entry_dabort`: `sub lr, lr, #8`; save r0-r12+lr, save spsr, switch to SVC mode.
4. `bl handle_data_abort(ctx)`.
5. `fault_from_user(ctx)`: `ctx->spsr & 0x1F == 0x10` → true.
6. Print `[FAULT]` log + `[KILL] pid=X`, `current->state = TASK_DEAD`, `scheduler_request_resched()`, `schedule()`.
7. `schedule()` picks the next READY process → `context_switch(dead, next)`.
8. Kernel continues in `next`'s context. The dead process's saved frame stays on its kstack but is never read.

Counter + runaway keep running. Shell dies, the kernel lives.

---

## Implementation

### Files

| File | Role |
|---|---|
| [kernel/include/syscall.h](../../kernel/include/syscall.h) | `SYS_*` constants + `syscall_dispatch` prototype |
| [kernel/syscall/syscall.c](../../kernel/syscall/syscall.c) | Dispatch table + Tier-1 handlers + `valid_user_ptr` |
| [kernel/arch/arm/exception/exception_handlers.c](../../kernel/arch/arm/exception/exception_handlers.c) | `handle_svc` calls dispatch + `schedule()`; abort handlers check `fault_from_user` |
| [kernel/sched/scheduler.c](../../kernel/sched/scheduler.c) | `scheduler_request_resched()` (used by yield/exit) |
| [user/libc/syscall.h](../../user/libc/syscall.h) | Inline-asm wrappers (`sys_write`, `sys_getpid`, `sys_yield`, `sys_exit`) |

### Key points

**Dispatch table** — a static const array, indexed by syscall number:
```c
typedef long (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

static const syscall_fn_t syscall_table[NR_SYSCALLS] = {
    [SYS_WRITE]  = sys_write,
    [SYS_GETPID] = sys_getpid,
    [SYS_YIELD]  = sys_yield,
    [SYS_EXIT]   = sys_exit,
};
```

Adding a new syscall in chapter 09: just add a constant, a handler, and one line in
the table.

**Inline-asm wrapper** uses `register asm("rN")` to force GCC to place values into the
correct registers:
```c
static inline int sys_write(int fd, const char *buf, unsigned int len) {
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
```

The constraint `"+r"(r0)` = input AND output through r0 — required so the compiler
knows the result comes back through r0 after `svc`. Without `+r` (only `=r` or `r`)
→ compiler reads the return value from the wrong register → function returns garbage.

**Fault isolation helper** follows the shared pattern:
```c
if (fault_from_user(ctx)) {
    uart_printf("[FAULT] ... PC=0x%08x\n", ctx->lr);
    user_fault_kill("data abort");   /* or "prefetch", "undefined" */
    return;                           /* unreachable if switched away */
}
/* kernel fault path */
uart_printf("\n[PANIC] ***...\n");
dump_context(ctx);
halt_forever();
```

---

## Testing

**Smoke test:** chapter 08 runs 3 real user programs. At each timer tick you see
counter print "[pid X] count=N" through `sys_write` — proof that dispatch + user
pointer validation + return path all work.

**Fault isolation test:** temporarily add `*(int*)0 = 0xDEAD` in counter after a few
iterations. Expect: `[FAULT] data abort DFAR=0x00000000 ... [KILL] pid=0`, then
runaway + shell still run. Revert once confirmed (don't ship crash code in the
product).

**Invalid SVC number:** user sets `r7 = 99` then `svc #0`. The dispatcher returns
`E_BADCALL = -1`. User sees a negative return, kernel doesn't crash.

---

## Links

### Dependencies

- **Chapter 02 — Exceptions**: vector table + `exception_entry_svc`.
- **Chapter 06 — Scheduler**: `schedule()` at the tail, `scheduler_request_resched`.
- **CLAUDE.md**: "Syscall pointer validation — the kernel never derefs a pointer
  from user space directly. Validate the VA lies within `0x40000000`–`0x40FFFFFF`".

### Next

**Chapter 08 — Userspace →** user space now has an ABI into the kernel. Time to
write real user programs in C: `crt0.S` handles entry + `sys_exit` when main
returns, `libc` wraps syscalls into friendly C functions (`ulib_puts`, `ulib_putu`),
and each app is built separately then `.incbin`'d into the kernel image.

---

<a id="tiếng-việt"></a>

**Tiếng Việt** · [English](#english)

> 3 process đã chạy luân phiên ở USR mode nhưng đang chơi trò câm. Chúng không chạm
> được UART (register nằm ngoài `0x40000000`–`0x40100000`), không gọi được hàm kernel,
> thậm chí không biết pid của mình. Chapter này mở một cánh cửa duy nhất: `svc #0`.
> User đặt số syscall vào `r7`, args vào `r0-r3`, CPU trap vào SVC mode, kernel tra
> bảng function pointer, thực thi, trả kết quả qua `r0`. Cửa đơn giản đến mức có thể
> audit — nhưng đủ mạnh để xây cả userland lên trên.

---

## Đã xây dựng đến đâu

Module có dấu ★ là **mới trong chapter này**.

```
┌──────────────────────────────────────────────────────┐
│                     User space                       │
│                                                      │
│   counter.c, runaway.c, shell.c (còn stub)           │
│             └── sys_write/getpid/yield/exit ─┐       │
│                                              │       │
│                  svc #0 (r7, r0-r3)          │       │
└──────────────────────────────────────────────┼───────┘
                                               ▼
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌──────────────────────────────────────────────────────┐
│                    Kernel (SVC mode)                 │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ handle_svc → syscall_dispatch          │       │
│   │      read ctx->r[7] + args               │       │
│   │      syscall_table[num](r0..r3)          │       │
│   │      store return → ctx->r[0]            │       │
│   │      schedule() at tail                  │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ Tier-1 syscall handlers                │       │
│   │      sys_write (UART + user ptr check)   │       │
│   │      sys_getpid                           │       │
│   │      sys_yield  (set need_reschedule)    │       │
│   │      sys_exit   (state = DEAD + resched) │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ Fault isolation in abort handlers      │       │
│   │      SPSR.mode == USR → kill current     │       │
│   │      else → panic (kernel fault)         │       │
│   └──────────────────────────────────────────┘       │
│                                                      │
│   Scheduler · Context switch · IRQ · MMU · UART     │
└──────────────────────────────────────────────────────┘
```

**Flow một syscall:**

```mermaid
sequenceDiagram
    participant U as user (USR)
    participant V as vector table
    participant E as exception_entry_svc
    participant D as syscall_dispatch
    participant H as handler (sys_write)
    participant S as schedule

    U->>V: svc #0 (r7=SYS_WRITE, r0-r3 args)
    Note over V: CPU: LR_svc=PC+4<br/>SPSR_svc=CPSR<br/>jump VBAR+0x08
    V->>E: ldr pc, _vec_svc
    E->>E: stmfd sp!, {r0-r12, lr}<br/>mrs r0, spsr; stmfd {r0}
    E->>D: bl handle_svc → syscall_dispatch(ctx)
    D->>H: syscall_table[ctx.r[7]](ctx.r[0..3])
    H->>H: validate user ptr<br/>do the work<br/>return value
    H-->>D: long ret
    D->>D: ctx.r[0] = ret
    D-->>E: handle_svc returns
    E->>S: schedule() tail
    Note over S: may context_switch<br/>or just return
    E->>E: ldmfd {r0} → msr spsr_cxsf<br/>ldmfd {r0-r12, pc}^
    E->>U: back to USR, r0 = ret
```

---

## Nguyên lý

### ABI là "hợp đồng" giữa user và kernel

Không có magic. Chỉ là convention hai bên đồng ý. Cortex-A-OS dùng phong cách Linux ARM
EABI:

| Register | Vai trò |
|---|---|
| `r7` | Syscall number |
| `r0..r3` | Argument 0..3 |
| `r0` (on return) | Kết quả (âm = lỗi) |

User side (wrapper inline asm):
```c
register int r0 __asm__("r0") = fd;
register const char *r1 __asm__("r1") = buf;
register unsigned int r2 __asm__("r2") = len;
register int r7 __asm__("r7") = SYS_WRITE;
__asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
return r0;
```

Kernel side (dispatch):
```c
void syscall_dispatch(exception_context_t *ctx) {
    uint32_t num = ctx->r[7];
    if (num >= NR_SYSCALLS || !syscall_table[num]) {
        ctx->r[0] = E_BADCALL;
        return;
    }
    ctx->r[0] = syscall_table[num](ctx->r[0], ctx->r[1], ctx->r[2], ctx->r[3]);
}
```

Bảng `syscall_table` là mảng function pointer index bởi số syscall. Thêm syscall
mới = thêm một entry.

### User pointer validation — luật đầu tiên

Kernel **không bao giờ** deref pointer user cung cấp mà không kiểm tra trước. Pointer
có thể trỏ vào kernel space, vào memory chưa map, vào vùng đọc-write trái phép.
Deref thẳng → kernel trap → system panic khi user làm sai.

Ranh giới đơn giản trên Cortex-A-OS: user chỉ có 1 MB tại `USER_VIRT_BASE = 0x40000000`.
Mọi pointer hợp lệ nằm trong `[0x40000000, 0x40100000)`.

```c
static int valid_user_ptr(const void *p, uint32_t len) {
    uint32_t addr = (uint32_t)p;
    uint32_t end  = addr + len;
    if (end < addr)                               return 0;  /* overflow */
    if (addr < USER_VIRT_BASE)                    return 0;
    if (end > USER_VIRT_BASE + USER_REGION_SIZE)  return 0;
    return 1;
}
```

Đây là phiên bản đơn giản của `copy_from_user` / `copy_to_user` trong Linux. Dự án
thật sẽ thay bằng hàm copy qua page-fault handler (nếu user pointer trỏ vùng chưa
paged in, page fault handler cài page, syscall retry). Cortex-A-OS không có demand
paging nên check + direct access đủ dùng.

### Fault isolation: user crash ≠ kernel crash

Data Abort, Prefetch Abort, Undefined Instruction đều do user code sai. Chapter 02
có handler fatal — in thông tin rồi halt. Giờ đã có multi-process, halt nghĩa là
giết toàn bộ system vì 1 process buggy. Phải phân biệt:

```c
static int fault_from_user(const exception_context_t *ctx) {
    return (ctx->spsr & 0x1FU) == 0x10U;   /* mode bits == USR */
}

void handle_data_abort(exception_context_t *ctx) {
    if (fault_from_user(ctx)) {
        uart_printf("[KILL] pid=%u ...\n", current->pid);
        current->state = TASK_DEAD;
        scheduler_request_resched();
        schedule();
        /* unreachable if any other process is runnable */
    }
    /* kernel fault → panic như cũ */
    halt_forever();
}
```

Cùng pattern cho prefetch abort, undefined instruction. Saved SPSR (được push bởi
exception entry) là CPSR **trước khi** fault xảy ra — đây là tín hiệu "ai đang
chạy khi sai".

### Tail `schedule()` — yield phải có hiệu lực ngay

`sys_yield` và `sys_exit` chỉ đặt `need_reschedule = 1`. Nếu không ai gọi
`schedule()` sau khi handler return, flag vô dụng đến timer IRQ kế. Fix: `handle_svc`
gọi `schedule()` ở tail, giống `handle_irq`:

```c
void handle_svc(exception_context_t *ctx) {
    syscall_dispatch(ctx);
    schedule();
}
```

Tại thời điểm tail, SVC frame đã settle trên kernel stack — context_switch tráo SP
an toàn. Nếu `need_reschedule` không set (syscall "thường" như `write`, `getpid`),
`schedule()` no-op ngay.

---

## Bối cảnh

```
Trạng thái trước chapter 07:
- 3 process chạy USR mode, round-robin bởi timer IRQ (chapter 06)
- handle_svc chỉ in "[SVC] syscall #N" rồi return (chapter 03 test T5)
- Abort handlers vẫn halt_forever cho mọi fault
- user program ban đầu là asm spin loop — chưa có user logic thật
```

---

## Vấn đề

1. **User không làm gì có ích.** Không I/O, không biết pid, không thoát được chính nó.
2. **Handle_svc chỉ là placeholder.** Không đọc r7, không dispatch, không trả kết quả.
3. **Mọi crash user giết luôn kernel.** Không có isolation — không demo được multi-process thật.
4. **Yield cooperative không work.** Ngay cả khi user có `sys_yield`, nếu flag không được consume đúng lúc thì yield nửa vời.

---

## Thiết kế

### Syscall numbers cố định

```c
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_EXIT        3
```

Giá trị không được đổi — đã được encode vào user binary. Tier 2 (`read`, `ps`, `kill`)
để chapter 09.

### 4 handler Tier-1

| Handler | Signature | Việc chính |
|---|---|---|
| `sys_write(fd, buf, len)` | — | `valid_user_ptr` → `uart_putc` từng byte → return `len` |
| `sys_getpid()` | — | return `current->pid` |
| `sys_yield()` | — | `scheduler_request_resched()` → return 0 |
| `sys_exit()` | noreturn | `current->state = TASK_DEAD` + resched |

`sys_write` bỏ qua `fd` — mọi fd đều đi UART. Phức tạp hơn không cần thiết cho scope
hiện tại.

### Fault handler: kill từ user, panic nếu kernel

Helper `user_fault_kill(kind)`:
```c
static void user_fault_kill(const char *kind) {
    if (current) {
        uart_printf("[KILL] pid=%u name=%s killed by %s\n",
                    current->pid, current->name, kind);
        current->state = TASK_DEAD;
    }
    scheduler_request_resched();
    schedule();
    /* fallback if no other runnable */
    halt_forever();
}
```

Cả 3 handler (data/prefetch/undef abort) check `fault_from_user(ctx)` đầu function:
true → gọi helper, false → giữ panic path cũ.

---

## Cách hoạt động

### Một lần write

User:
```c
sys_write(1, "hello\n", 6);
```

Flow:
1. Wrapper set `r0=1, r1=&"hello\n", r2=6, r7=0`.
2. `svc #0` → CPU: `LR_svc = PC+4`, `SPSR_svc = CPSR`, jump `VBAR + 0x08`.
3. Vector: `ldr pc, _vec_svc` → `exception_entry_svc`.
4. Entry: `stmfd sp!, {r0-r12, lr}`; `mrs r0, spsr; stmfd {r0}`; `mov r0, sp` (= ctx).
5. `bl handle_svc` — ctx trỏ vào frame trên SVC stack.
6. `syscall_dispatch(ctx)`: read `ctx->r[7] = 0` → `sys_write(1, buf_addr, 6, _)`.
7. `sys_write`: `valid_user_ptr(buf, 6)` pass → loop `uart_putc(buf[i])` → return 6.
8. dispatch: `ctx->r[0] = 6`.
9. Back to `handle_svc` tail → `schedule()` (no-op vì `need_reschedule==0`).
10. Entry: `ldmfd sp!, {r0}; msr spsr_cxsf, r0; ldmfd sp!, {r0-r12, pc}^`.
11. User thấy `r0 = 6`.

### Crash từ user

User:
```c
*(volatile int *)0 = 0xDEAD;
```

Flow:
1. CPU write to VA 0: MMU walk proc_pgd entry 0 = FAULT → **Data Abort**.
2. `LR_abt = PC + 8`, `SPSR_abt = CPSR (USR mode, bit[4:0] = 0x10)`, jump `VBAR + 0x10`.
3. `exception_entry_dabort`: `sub lr, lr, #8`; save r0-r12+lr, save spsr, switch SVC mode.
4. `bl handle_data_abort(ctx)`.
5. `fault_from_user(ctx)`: `ctx->spsr & 0x1F == 0x10` → true.
6. Print `[FAULT]` log + `[KILL] pid=X`, `current->state = TASK_DEAD`, `scheduler_request_resched()`, `schedule()`.
7. `schedule()` picks next READY process → `context_switch(dead, next)`.
8. Kernel continues in `next`'s context. Dead process's saved frame remains on its kstack but never read.

Counter + runaway tiếp tục chạy. Shell chết, kernel sống.

---

## Implementation

### Files

| File | Vai trò |
|---|---|
| [kernel/include/syscall.h](../../kernel/include/syscall.h) | `SYS_*` constants + `syscall_dispatch` prototype |
| [kernel/syscall/syscall.c](../../kernel/syscall/syscall.c) | Dispatch table + Tier-1 handlers + `valid_user_ptr` |
| [kernel/arch/arm/exception/exception_handlers.c](../../kernel/arch/arm/exception/exception_handlers.c) | `handle_svc` gọi dispatch + `schedule()`; abort handlers check `fault_from_user` |
| [kernel/sched/scheduler.c](../../kernel/sched/scheduler.c) | `scheduler_request_resched()` (dùng cho yield/exit) |
| [user/libc/syscall.h](../../user/libc/syscall.h) | Inline asm wrappers (`sys_write`, `sys_getpid`, `sys_yield`, `sys_exit`) |

### Điểm chính

**Dispatch table** — static const array, index bằng syscall #:
```c
typedef long (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

static const syscall_fn_t syscall_table[NR_SYSCALLS] = {
    [SYS_WRITE]  = sys_write,
    [SYS_GETPID] = sys_getpid,
    [SYS_YIELD]  = sys_yield,
    [SYS_EXIT]   = sys_exit,
};
```

Thêm syscall mới ở chapter 09: chỉ cần thêm constant, handler, một dòng trong table.

**Inline asm wrapper** dùng `register asm("rN")` để ép GCC đưa giá trị vào đúng
register:
```c
static inline int sys_write(int fd, const char *buf, unsigned int len) {
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
```

Constraint `"+r"(r0)` = đầu vào VÀ đầu ra qua r0 — bắt buộc để compiler biết kết
quả đi qua r0 sau `svc`. Thiếu `+r` (chỉ `=r` hoặc `r`) → compiler đọc return từ
register sai → function trả về giá trị rác.

**Fault isolation helper** đi theo pattern chung:
```c
if (fault_from_user(ctx)) {
    uart_printf("[FAULT] ... PC=0x%08x\n", ctx->lr);
    user_fault_kill("data abort");   /* hoặc "prefetch", "undefined" */
    return;                           /* unreachable if switched away */
}
/* kernel fault path */
uart_printf("\n[PANIC] ***...\n");
dump_context(ctx);
halt_forever();
```

---

## Testing

**Smoke test:** chapter 08 sẽ chạy 3 user program thật. Mỗi tick timer thấy
counter in "[pid X] count=N" qua `sys_write` — chứng minh syscall dispatch + user
pointer validation + return path hoạt động.

**Fault isolation test:** tạm cho counter `*(int*)0 = 0xDEAD` sau vài iteration.
Expect: `[FAULT] data abort DFAR=0x00000000 ... [KILL] pid=0`, sau đó runaway +
shell vẫn chạy. Revert khi xác nhận xong (không để crash code trong sản phẩm).

**SVC number invalid:** user set `r7 = 99` rồi `svc #0`. Dispatcher trả
`E_BADCALL = -1`. User thấy return âm, kernel không crash.

---

## Liên kết

### Dependencies

- **Chapter 02 — Exceptions**: vector table + `exception_entry_svc`.
- **Chapter 06 — Scheduler**: `schedule()` tại tail, `scheduler_request_resched`.
- **CLAUDE.md**: "Syscall pointer validation — kernel không bao giờ deref pointer
  từ user space trực tiếp. Validate VA nằm trong `0x40000000`–`0x40FFFFFF`".

### Tiếp theo

**Chapter 08 — Userspace →** user đã có ABI vào kernel. Giờ đến lúc viết user
program thật bằng C: `crt0.S` làm entry + `sys_exit` khi main return, `libc` bọc
syscall thành hàm C dễ dùng (`ulib_puts`, `ulib_putu`), per-process binary build
riêng rồi `.incbin` vào kernel image.
