# Cortex-A-OS — ARMv7-A Bare-Metal Kernel Design Document

> **Target:** QEMU realview-pb-a8 (development) + BeagleBone Black AM335x (deployment)
> **CPU:** ARM Cortex-A8 (ARMv7-A, single-core)
> **Toolchain:** GNU ARM EABI, Makefile-based build
> **Code:** ~5000 lines C + GNU as, 42 files, 20 directories

---

## 1. System Overview

### 1.1 Mục đích

Cortex-A-OS là một bare-metal kernel viết từ đầu trên ARMv7-A, mục tiêu duy nhất là hiểu OS hoạt động bên trong bằng cách tự xây dựng từng thành phần cốt lõi: boot sequence, MMU, exception handling, context switch, preemptive scheduling, syscall interface, và interactive shell.

### 1.2 Hardware Targets

| Thuộc tính | QEMU realview-pb-a8 | BeagleBone Black (AM335x) |
|---|---|---|
| SoC | ARM Cortex-A8 MPcore | TI AM335x (Cortex-A8) |
| Physical RAM | 128 MB [0x70000000, 0x78000000) | 512 MB [0x80000000, 0xA0000000) |
| Peripheral bus | AMBA AXI (PL011, SP804, GIC) | L4_WKUP + L4_PER (NS16550, DMTIMER, INTC) |
| Interrupt controller | GIC v1 (distributor + CPU interface) | AM335x INTC (single block) |
| Boot method | `-kernel` ELF loading | SPL → MLO → kernel binary |

### 1.3 Kernel Architecture at a Glance

- **Virtual memory:** 3G/1G split (user low, kernel high), section-only L1 page tables (1 MB granularity)
- **Scheduling:** Preemptive round-robin, 10 ms tick, BLOCKED state for I/O
- **Processes:** 3 static processes, each with isolated 1 MB user PA slot
- **Syscall ABI:** Linux ARM EABI (`r7` = number, `r0-r3` = args, `svc #0`)
- **Driver model:** Subsystem core + chip driver ops (UART, timer, interrupt controller)
- **Platform separation:** Build-time selection (`PLATFORM=qemu|bbb`), not preprocessor `#ifdef`

### 1.4 Design Philosophy

Cortex-A-OS mượn tư duy thiết kế từ Linux ARM 32-bit: 3G/1G split, per-process page table, syscall qua SVC, preemptive scheduler. Tuy nhiên được đơn giản hóa triệt để: không L2 page table, không fork/exec, không dynamic allocation, không SMP.

### 1.5 Out of Scope

Filesystem, networking, fork/exec, signals, pipe, socket, IPC, custom bootloader, POSIX compliance, display/HDMI, dynamic memory allocation (`kmalloc`/`free`), SMP.

---

## 2. Platform Abstraction Layer

### 2.1 Build-Time Platform Selection

Platform được chọn tại build time qua biến `PLATFORM` trong Makefile. Mặc định là `qemu`:

```bash
make               # PLATFORM=qemu
make PLATFORM=bbb  # build cho BeagleBone Black
```

Cơ chế: Makefile dùng `-include kernel/platform/$(PLATFORM)/platform.mk` để lấy danh sách chip driver, và `-I kernel/platform/$(PLATFORM)` để include đúng `board.h`. Linker script tương ứng: `kernel/linker/kernel_$(PLATFORM).ld`.

**Không dùng preprocessor `#ifdef PLATFORM_QEMU` / `#ifdef PLATFORM_BBB`** — platform selection hoàn toàn ở file level. File của platform này không bao giờ được compiled khi build cho platform kia.

### 2.2 Per-Platform File Layout

```
kernel/platform/<platform>/
├── board.h          # Numeric constants: RAM_BASE, PHYS_OFFSET, UART0_BASE, IRQ numbers, clock rates
├── board.c          # platform_init_devices() — wires chip driver ops to board addresses
├── periph_map.c     # platform_map_peripherals() — installs identity-mapped MMIO sections
└── platform.mk      # Lists exactly 3 chip driver files

kernel/linker/
├── kernel_qemu.ld   # VMA 0xC0100000, LMA 0x70100000
└── kernel_bbb.ld    # VMA 0xC0000000, LMA 0x80000000
```

### 2.3 Shared Contract Header

**File:** `kernel/include/platform.h`

Cung cấp các hằng số và prototype mà platform code phải implement:

```c
#define KERNEL_VIRT_BASE    0xC0000000U  /* High-VA base */
#define USER_VIRT_BASE      0x40000000U  /* User section VA */
#define USER_REGION_SIZE    0x00100000U  /* 1 MB per process */
#define NUM_PROCESSES       3U
#define KSTACK_SIZE         8192U

void platform_init_devices(void);       /* board.c */
void platform_map_peripherals(uint32_t *pgd);  /* periph_map.c */
```

Mọi platform phải define trong `board.h`:
- `PLATFORM_NAME` — tên platform (dùng trong boot banner)
- `RAM_BASE`, `RAM_SIZE` — physical RAM range
- `KERNEL_PHYS_BASE` — địa chỉ load kernel
- `PHYS_OFFSET` — `VA_KERNEL_BASE - RAM_BASE`
- `UART0_BASE`, `TIMER0_BASE` hoặc `TIMER2_BASE` — device base addresses
- `IRQ_TIMER`, `IRQ_UART0` — interrupt lines
- `TIMER_CLK_HZ` — timer input clock frequency
- `USER_PHYS_BASE`, `USER_PHYS_STRIDE` — user physical slot base và stride

### 2.4 Invariant

File của QEMU platform không bao giờ được compiled khi build cho BBB, và ngược lại. Include path `-I kernel/platform/$(PLATFORM)` chọn đúng `board.h`.

### 2.5 QEMU System Profile

| Parameter | Value |
|---|---|
| Physical RAM | 128 MB at 0x70000000 |
| Kernel load PA | 0x70100000 |
| Kernel link VA | 0xC0100000 |
| `PHYS_OFFSET` | 0x50000000 (VA_KERNEL_BASE - RAM_BASE) |
| UART0 base | 0x10009000 (PL011) |
| Timer base | 0x10011000 (SP804, 1 MHz) |
| IRQ: Timer0 | 36 |
| IRQ: UART0 | 44 |
| User PA slots | 0x70200000 (counter), 0x70300000 (runaway), 0x70400000 (shell) |

Peripheral identity map:
- `[0x10000000, 0x10200000)` — 2 MB: PL011 + SP804
- `[0x1E000000, 0x1E100000)` — 1 MB: GIC v1

### 2.6 BeagleBone Black System Profile

| Parameter | Value |
|---|---|
| Physical RAM | 512 MB at 0x80000000 |
| Kernel load PA | 0x80000000 |
| Kernel link VA | 0xC0000000 |
| `PHYS_OFFSET` | 0x40000000 (VA_KERNEL_BASE - RAM_BASE) |
| UART0 base | 0x44E09000 (NS16550) |
| Timer2 base | 0x48040000 (DMTIMER, 24 MHz CLK_M_OSC) |
| IRQ: DMTIMER2 | 68 |
| IRQ: UART0 | 72 |
| User PA slots | 0x80200000 (counter), 0x80300000 (runaway), 0x80400000 (shell) |

Peripheral identity map:
- `[0x44E00000, 0x44F00000)` — 1 MB: L4_WKUP (UART0, CM_PER)
- `[0x48000000, 0x49000000)` — 16 MB: L4_PER (DMTIMER2, INTC, GPIO)

Clock gating (BBB-specific): `board.c` phải enable clock cho DMTIMER2 qua CM_PER trước khi timer driver init — chọn `CLK_M_OSC` (24 MHz), wake L4LS clock domain.

---

## 3. Boot Architecture

### 3.1 Entry Point

CPU vào kernel ở physical address, MMU off. File `kernel/arch/arm/boot/start.S`:

```asm
_start:
    b   _reset              @ Relative branch (BBB SPL sanity check)
    ...
_reset:
    cpsid if                @ Mask IRQ + FIQ immediately
```

### 3.2 Pre-MMU Sequence (all at PA)

1. **Mask interrupts** — `cpsid if`
2. **Compute `PHYS_OFFGET`**:
   ```asm
   adr r0, _start           @ r0 = PA of _start (PC-relative)
   ldr r4, =_start          @ r4 = VA of _start (literal pool)
   sub r4, r4, r0           @ r4 = PHYS_OFFSET = VA - PA
   ```
   QEMU: `0xC0100000 - 0x70100000 = 0x50000000`
   BBB: `0xC0000000 - 0x80000000 = 0x40000000`
3. **Temporary SVC stack tại PA** — `sub sp, r1, r4` (convert VA symbol `_svc_stack_top` to PA)
4. **Zero BSS tại PA** — loop `_bss_start`..`_bss_end` minus `PHYS_OFFSET`
5. **Call `mmu_init(phys_offset)`** — xây boot page table tại PA, enable MMU

### 3.3 MMU Enable Sequence

**File:** `kernel/arch/arm/mm/mmu_enable.S`

```
DSB → ICIALLU → DSB → ISB         @ Clean pipeline
TTBR0 = pgd_pa | 0x4A              @ Inner/outer WB-WA, shareable
TTBCR = 0                          @ TTBR0 covers full 4 GB
DACR = 0x1                         @ Domain 0 = Client (AP enforced)
TLBIALL → DSB → ISB                @ Flush old TLB
SCTLR |= M(0) | C(2) | Z(11) | I(12)  @ MMU + DCache + BranchPred + ICache
ISB
```

`0x4A` decoding: S=1 (shareable), RGN=01 (outer WB-WA), IRGN=01 (inner WB-WA). Domain 0 ở Client mode — AP bits là protection boundary duy nhất giữa user và kernel.

### 3.4 VA Trampoline

```asm
ldr pc, =_start_va           @ Absolute load → PC vào high-VA alias
```

Sau instruction này, CPU fetch instruction qua MMU. Từ `_start_va` trở đi, kernel chạy hoàn toàn ở high VA.

### 3.5 Post-Trampoline Stack Setup

Tại `_start_va` (VA mode), reseat tất cả exception mode stacks bằng absolute symbol loads (không cần subtract PHYS_OFFSET):

| Mode | Stack size | Purpose |
|---|---|---|
| FIQ | 512 B | Unused (halt in entry) |
| IRQ | 1 KB | Trampoline only (srsdb → SVC) |
| ABT | 1 KB | Trampoline only |
| UND | 1 KB | Trampoline only |
| SVC | 8 KB | Main kernel stack (trước process init) |

Sau process init, mỗi process có kernel stack SVC 8 KB riêng — shared exception stacks chỉ dùng làm trampoline.

### 3.6 kmain Initialization Order

```
platform_init_devices()    → Wire chip drivers to board addresses
uart_init()                → Console operational
exception_init()           → Set VBAR
irq_init()                 → Zero dispatch table, init INTC chip
timer_init(10000)          → 10 ms tick
irq_register(IRQ_TIMER, timer_irq) + irq_enable()
irq_register(IRQ_UART0, uart_rx_irq) + irq_enable()
process_init_all()         → Build all 3 PCBs
mmu_drop_identity()        → Remove PA identity map
timer_set_handler(scheduler_tick)  → Arm preemptive scheduling
process_first_run(&processes[0])   → First user process entry (noreturn)
```

### 3.7 Identity Map Lifecycle

1. **Created** trong `mmu_build_boot_pgd()`: identity-map `[RAM_BASE, RAM_BASE + RAM_SIZE)` với attribute `PDE_KERNEL_MEM`
2. **Used** trong ~20 instructions giữa MMU enable (`mmu_enable.S`) và VA trampoline (`ldr pc, =_start_va`)
3. **Dropped** trong `mmu_drop_identity()`: clear các PGD entries từ `RAM_BASE >> 20` đến `(RAM_BASE + RAM_SIZE) >> 20`, sau đó `TLBIALL + DSB + ISB`

Sau `mmu_drop_identity()`, mọi PA dereference ngẫu nhiên đều gây Data Abort ngay lập tức — đây là cơ chế bảo vệ chống bug kernel.

### 3.8 Platform Boot Differences

| Aspect | QEMU | BBB |
|---|---|---|
| Boot loader | `-kernel` loads ELF directly | SPL → MLO → kernel at 0x80000000 |
| Kernel load PA | 0x70100000 (1 MB vào RAM) | 0x80000000 (base of RAM) |
| Kernel link VA | 0xC0100000 | 0xC0000000 |
| `PHYS_OFFSET` | 0x50000000 | 0x40000000 |
| BSS location | .bss section in ELF | .bss section in ELF (zeroed by SPL?) |

**Quan trọng:** Trên BBB, kernel được load tại đầu RAM (0x80000000) nên kernel link VA = 0xC0000000 (trùng với `VA_KERNEL_BASE`). Trên QEMU, kernel load tại 0x70100000 (sau 1 MB null guard), nên link VA = 0xC0100000. `PHYS_OFFSET` khác nhau giữa 2 platform do layout RAM khác nhau.

---

## 4. Memory Management Subsystem

### 4.1 Page Table Format

Cortex-A-OS chỉ dùng L1 section descriptors (1 MB granularity). Mỗi page table có 4096 entries, mỗi entry 32-bit, tổng 16 KB.

| Entry | VA range | Purpose |
|---|---|---|
| 0 | [0x00000000, 0x00100000) | NULL guard (FAULT) |
| 1024 (0x400) | [0x40000000, 0x40100000) | User section per-process |
| 3072 (0xC00) | [0xC0000000, 0xC0000000 + RAM_SIZE) | Kernel high alias |

### 4.2 Section Attributes

Domain 0 duy nhất. `DACR = 0x1` → domain 0 ở Client mode (AP bits enforced).

| Macro | PDE value | Access | Execute | Cache |
|---|---|---|---|---|
| `PDE_TYPE_FAULT` | 0x0 | No access | No | N/A |
| `PDE_KERNEL_MEM` | section \| C \| B \| AP_KERN_RW \| DOMAIN(0) | Kernel RW, user none | Yes | WB-WA |
| `PDE_DEVICE` | section \| AP_KERN_RW \| DOMAIN(0) \| XN | Kernel RW, user none | XN | Strongly ordered |
| `PDE_USER_TEXT` | section \| C \| B \| AP_KERN_USER_RW \| DOMAIN(0) | Kernel RW, user RW | Yes | WB-WA |

- `AP_KERN_RW` = AP[1:0] = 01 (privileged RW, no user access)
- `AP_KERN_USER_RW` = AP[1:0] = 11 (privileged RW, user RW)

### 4.3 Boot Page Table

**File:** `kernel/arch/arm/mm/pgtable.c`

```c
static uint32_t boot_pgd[4096] __attribute__((section(".bss.pgd"), aligned(16384)));
```

`mmu_build_boot_pgd(pgd)` xây dựng:
1. **Identity map RAM** — `pgd[RAM_BASE >> 20]` → `PDE_KERNEL_MEM`
2. **Kernel high alias** — `pgd[VA_KERNEL_BASE >> 20]` → `PDE_KERNEL_MEM` (map tới cùng PA range)
3. **Platform peripherals** — gọi `platform_map_peripherals(pgd)` cho device MMIO
4. **NULL guard** — entry 0 left zero (FAULT)

### 4.4 Per-Process Page Tables

Mỗi process có L1 table riêng trong `proc_pgd[3][4096]` (BSS, 16 KB aligned).

`pgtable_build_for_proc(pgd, user_pa)`:
1. Zero tất cả 4096 entries
2. Copy non-zero entries từ `boot_pgd` (trừ identity RAM range)
3. Install user section: `pgd[USER_VIRT_BASE >> 20] = user_pa | PDE_USER_TEXT`
4. `DSB` barrier

### 4.5 Address-Space Switch

Trong `context_switch.S`:

```
1. Save prev callee-saved regs (r4-r11, lr) lên SVC stack
2. Save prev banked SP_usr, LR_usr vào PCB
3. TTBR0 = next->pgd_pa | 0x4A
4. TLBIALL + ICIALLU + DSB + ISB
5. Restore next banked SP_usr, LR_usr từ PCB
6. Restore next SVC stack pointer
7. Pop {r4-r11, lr}, bx lr
```

### 4.6 I-Cache Synchronization

User VA `0x40000000` maps to different PA per process. Cần I-cache invalidation tại 2 điểm:

1. **Sau `process_init_all()`** — `icache_sync()` clean D-cache (DCCMVAU) + ICIALLU + DSB + ISB
2. **Mỗi context switch** — ICIALLU + DSB + ISB

### 4.7 Design Limits

- Section-only mapping: user section là RWX, không có W^X separation
- No L2 page tables, no demand paging, no page allocator
- No per-thread stack guard page trong user 1 MB window
- Tất cả page table 16 KB aligned, user PA slot 1 MB aligned

---

## 5. Process Model

### 5.1 Static Process Table

```c
process_t processes[NUM_PROCESSES];  // NUM_PROCESSES = 3
process_t *current;                  // Currently running process
```

Cả hai đều trong BSS, zero-initialized bởi `start.S`.

### 5.2 PCB Layout

```c
typedef struct {
    uint32_t r0..r12;       // [0..48] General-purpose registers
    uint32_t sp_svc;        // [52]    Kernel stack pointer
    uint32_t lr_svc;        // [56]    Kernel link register
    uint32_t spsr;          // [60]    Saved CPSR
    uint32_t sp_usr;        // [64]    User stack pointer (banked)
    uint32_t lr_usr;        // [68]    User link register (banked)
} proc_context_t;           // Total 72 bytes

typedef struct process {
    proc_context_t ctx;     // [0..71]
    uint32_t       pid;     // [72]
    task_state_t   state;   // [76]
    const char    *name;    // [80]
    uint32_t      *pgd;     // [84] VA of L1 table
    uint32_t       pgd_pa;  // [88] PA for TTBR0
    void          *kstack_base;
    uint32_t       kstack_size;  // 8192
    uint32_t       user_entry;       // = USER_VIRT_BASE
    uint32_t       user_stack_top;  // = USER_VIRT_BASE + 1 MB
    uint32_t       user_phys_base;  // per-process PA slot
} process_t;
```

Field offsets dùng trong assembly (`context_switch.S`) được enforce bằng `_Static_assert` trong `proc.h`.

### 5.3 Process States

```c
typedef enum {
    TASK_READY,     // 0 — có thể schedule
    TASK_RUNNING,   // 1 — đang chiếm CPU
    TASK_BLOCKED,   // 2 — chờ I/O (UART input)
    TASK_DEAD       // 3 — đã exit/kill/fault
} task_state_t;
```

```
         schedule()            yield / timer IRQ
  READY ──────────► RUNNING ──────────────► READY
                      │
                      │ sys_read (no data)
                      ▼
                   BLOCKED ──► READY   (UART RX IRQ wake)
                      │
                      │ sys_exit / kill / fault
                      ▼
                    DEAD        (scheduler skip)
```

### 5.4 Kernel Stack per Process

Mỗi process có 8 KB kernel stack riêng (BSS: `proc_kstack[3][8192]`). Chỉ SVC mode dùng stack này.

Các exception stack (IRQ/ABT/UND/FIQ) là shared trampoline stacks trong `.stack` section — tổng 11.5 KB (FIQ 512 B, IRQ 1 KB, ABT 1 KB, UND 1 KB, SVC 8 KB initial). Exception code chỉ dùng các stack này để save vài registers rồi switch ngay sang SVC mode + per-process stack.

### 5.5 25-Word Initial Stack Frame

Khi process chưa từng chạy, kernel pre-build một frame tại đỉnh kernel stack 8 KB:

```
High addr (0x2000 above kstack_base)
┌─────────────────────────┐
│ CPSR = 0x10 (USR mode)  │  ← first popped by rfefd
│ PC   = USER_VIRT_BASE   │
│ LR   = 0                │  16-word IRQ-exit frame
│ r12..r0 = 0             │  (consumed by ret_from_first_entry)
├─────────────────────────┤
│ LR   = ret_from_first_entry │
│ r11..r4 = 0             │  9-word kernel-resume frame
│                         │  (consumed by context_switch pop)
└─────────────────────────┘ ← ctx.sp_svc = kstack_top - 100
Low addr
```

**25 words = 100 bytes.** 9-word frame cho kernel-resume path (context_switch's `pop {r4-r11, lr}`), 16-word frame cho IRQ-exit path (`ldmfd {r0-r12, lr}` + `rfefd sp!`). Cả hai path dùng chung code: first-time entry là special case nơi context_switch nhận `prev=NULL` và `bx lr` trỏ vào `ret_from_first_entry`.

### 5.6 Process Initialization

`process_init_all()`:

```
for each process i:
  1. Clear PCB, set pid = i, state = READY, name
  2. pgd = &proc_pgd[i], pgd_pa = VA(pgd) - PHYS_OFFSET
  3. kstack = &proc_kstack[i]
  4. user_phys_base = USER_PHYS_BASE + i * USER_PHYS_STRIDE
  5. Copy user binary:
     user_va_alias = user_phys_base + PHYS_OFFSET  (via kernel high VA)
     kmemcpy(user_va_alias, img_start, img_size)
  6. icache_sync(user_va_alias, img_size)  (D-cache clean + I-cache invalidate)
  7. pgtable_build_for_proc(pgd, user_phys_base)
  8. process_build_initial_frame(p)
```

### 5.7 User Binary Embedding

User programs được build riêng thành `.bin` files (linked tại VA `0x40000000`), embed vào kernel image qua `.incbin` trong `kernel/arch/arm/proc/user_binaries.S`:

```asm
.section .user_binaries
_counter_img_start: .incbin "build/user/counter.bin"
_counter_img_end:
_runaway_img_start: .incbin "build/user/runaway.bin"
_runaway_img_end:
_shell_img_start:   .incbin "build/user/shell.bin"
_shell_img_end:
```

### 5.8 First Process Bootstrap

```c
void process_first_run(process_t *first) {
    context_switch(NULL, first);  // NULL prev → skip save side
}
```

`context_switch(NULL, first)`: bỏ qua save side, load side pop initial frame từ `first->ctx.sp_svc`, `bx lr` → `ret_from_first_entry` → `rfefd sp!` → USR mode tại `0x40000000`. `kmain()` không bao giờ return sau call này.

---

## 6. Scheduling Subsystem

### 6.1 Policy: Preemptive Round-Robin

- Time slice: 10 ms (một tick timer)
- `sys_yield`: voluntary relinquish phần còn lại của time slice
- `sys_exit` / fault: process rời run queue ngay lập tức

### 6.2 Flag-Driven Design

Timer IRQ handler không context-switch trực tiếp — chỉ set flag:

```c
static volatile int need_reschedule;

void scheduler_tick(void) {
    need_reschedule = 1;      // Called từ timer IRQ
}
```

Context switch thực sự xảy ra ở tail của `handle_irq()` hoặc `handle_svc()` qua `schedule()`. Điều này đảm bảo exception stack frame đã ổn định trước khi swap process.

### 6.3 Round-Robin Sweep

```c
void schedule(void) {
    if (!need_reschedule || !current) return;
    need_reschedule = 0;

    // wake_hint được ưu tiên (xem 6.5)
    if (wake_hint && wake_hint->state == TASK_READY) {
        perform_switch(current, wake_hint);
        wake_hint = NULL;
        return;
    }

    // Round-robin từ pid+1
    for (i = 1; i < NUM_PROCESSES; i++) {
        cand = &processes[(current->pid + i) % NUM_PROCESSES];
        if (cand->state == TASK_READY || cand->state == TASK_RUNNING) {
            perform_switch(current, cand);
            return;
        }
    }
    // Fall through: keep current
}
```

`perform_switch(prev, cand)`: demote prev→READY (nếu RUNNING), set cand→RUNNING, update `current`, gọi `context_switch(prev, cand)`.

### 6.4 BLOCKED-State I/O

Đây là cơ chế tránh busy-wait khi shell gọi `sys_read` mà chưa có input:

```c
void scheduler_block_on_input(void) {
    current->state = TASK_BLOCKED;
    blocked_reader = current;
    scheduler_request_resched();   // set flag + schedule()
    // Process ngừng chạy ở đây, UART IRQ sẽ wake
}

void scheduler_wake_reader(void) {
    if (blocked_reader && blocked_reader->state == TASK_BLOCKED) {
        blocked_reader->state = TASK_READY;
        wake_hint = blocked_reader;  // Priority boost
        blocked_reader = NULL;
        scheduler_request_resched();
    }
}
```

**Single-slot design**: chỉ shell dùng blocking read. Mở rộng cho nhiều reader cần wait queue.

### 6.5 wake_hint Priority Boost

`wake_hint` là optimization: khi UART RX IRQ wake blocked reader, reader được ưu tiên chọn trong `schedule()` kế tiếp (bypass round-robin sweep). Điều này tránh tình trạng I/O-bound process bị CPU-bound sibling "bỏ đói" trong plain round-robin.

### 6.6 Timer Subsystem Integration

```c
timer_set_handler(scheduler_tick);  // Set AFTER process_init_all()
```

Trước thời điểm này, `schedule()` là no-op vì `need_reschedule` luôn 0. Timer vẫn count tick nhưng không trigger preemption. Điều này ngăn scheduler làm gián đoạn boot initialization.

---

## 7. Exception and Interrupt Architecture

### 7.1 Vector Table

**File:** `kernel/arch/arm/exception/vectors.S`

32-byte aligned. 7 entries dùng LDR PC literal pool pattern:

| Offset | Exception | Handler |
|---|---|---|
| 0x00 | Reset | `_start` |
| 0x04 | Undefined Instruction | `exception_entry_undef` |
| 0x08 | SVC (syscall) | `exception_entry_svc` |
| 0x0C | Prefetch Abort | `exception_entry_pabort` |
| 0x10 | Data Abort | `exception_entry_dabort` |
| 0x14 | Reserved | NOP |
| 0x18 | IRQ | `exception_entry_irq` |
| 0x1C | FIQ | `exception_entry_fiq` |

VBAR được set trong `exception_init()` (gọi từ kmain, sau MMU init) qua `mcr p15, 0, r0, c12, c0, 0`.

### 7.2 Exception Context Layout

```c
typedef struct {
    uint32_t spsr;       // Saved CPSR
    uint32_t r[13];      // r0..r12
    uint32_t lr;         // Adjusted return address
} exception_context_t;
```

Layout trên stack: `[SP+0] = spsr`, `[SP+4..SP+52] = r0..r12`, `[SP+56] = lr`.

### 7.3 Exception-to-Kernel-Stack Flow

**Nguyên tắc:** Exception stack (IRQ/ABT/UND/FIQ) chỉ dùng làm trampoline — save tối thiểu rồi switch sang SVC mode + per-process kernel stack ngay.

```
1. CPU chuyển sang exception mode (ví dụ IRQ mode)
2. Save registers tạm lên exception stack (trampoline)
3. Switch sang SVC mode NGAY LẬP TỨC
4. Lấy kernel stack của process hiện tại
5. Save exception context lên per-process kernel stack
6. Xử lý exception
7. Restore context, return về user mode
```

### 7.4 SVC Entry

```
exception_entry_svc:
    push {r0-r12, lr}       @ Save GPRs + return address (LR_svc)
    mrs r0, spsr
    push {r0}               @ Save SPSR
    mov r0, sp              @ r0 = exception_context_t*
    bl  handle_svc
    pop {r0}                @ Restore SPSR
    msr spsr_cxfsf, r0
    pop {r0-r12, pc}^       @ Atomically return to USR mode
```

LR_svc = return address của syscall (không cần adjust). `handle_svc()` gọi `syscall_dispatch(ctx)` rồi `schedule()` ở tail.

### 7.5 IRQ Entry

```
exception_entry_irq:
    sub lr, lr, #4          @ Adjust return address
    srsdb sp!, #0x13        @ Push LR_irq + SPSR_irq lên SVC stack
    cps #0x13               @ Switch to SVC mode
    push {r0-r12, lr}       @ Save GPRs (lr = adjusted return)
    bl  handle_irq          @ irq_dispatch() + schedule()
    pop {r0-r12, lr}
    rfefd sp!               @ Atomically restore PC + CPSR
```

`srsdb sp!, #0x13` push LR_irq và SPSR_irq trực tiếp lên SVC stack — đây là cơ chế trampoline 1 instruction. `rfefd` restore cả PC và CPSR trong 1 instruction, unmask IRQs trên đường về user mode.

### 7.6 Fault Handlers (Data Abort, Prefetch Abort, Undefined)

Mỗi handler đọc fault status register tương ứng (DFSR/DFAR, IFSR/IFAR), kiểm tra SPSR mode bits:

- **User fault** (SPSR[4:0] = 0x10): gọi `user_fault_kill(kind)` → set current DEAD → schedule process khác. Kernel sống.
- **Kernel fault** (SPSR[4:0] != 0x10): dump registers + `halt_forever()` (WFI loop). Kernel bug.

`dump_context(ctx)`: in SPSR, mode, PC, r0-r12. `halt_forever()`: in [PANIC], WFI loop.

### 7.7 FIQ Entry

Không dùng. Infinite loop (halt).

### 7.8 Interrupt Controller Subsystem

**Core:** `kernel/drivers/intc/intc_core.c` — dispatch table `irq_table[128]`, cung cấp `irq_register()`, `irq_enable()`, `irq_dispatch()`.

**Chip drivers:**
- `gicv1.c` — QEMU: distributor + CPU interface, 128 SPI lines, PMR=0xF0
- `am335x_intc.c` — BBB: single block, soft reset, 128 lines (4 banks of 32), NEWIRQAGR EOI

`irq_dispatch()` flow:
```
active = intc->ops->get_active(intc)
if active < MAX_IRQS && irq_table[active] != NULL
    irq_table[active]()         @ Call registered handler
intc->ops->eoi(intc, active)    @ Always EOI
```

---

## 8. System Call Interface

### 8.1 ABI Convention

Linux ARM EABI:
| Register | Role |
|---|---|
| `r7` | Syscall number |
| `r0-r3` | Arguments 0..3 |
| `r0` | Return value (after `svc #0` returns) |

User process gọi `svc #0` → CPU vào SVC handler → `handle_svc()` → `syscall_dispatch()`.

### 8.2 Syscall Table

| # | Name | Signature | Returns |
|---|---|---|---|
| 0 | `write` | `sys_write(fd, buf, len)` | Bytes written, `E_FAULT` on bad ptr |
| 1 | `getpid` | `sys_getpid()` | `current->pid` |
| 2 | `yield` | `sys_yield()` | 0 |
| 3 | `exit` | `sys_exit()` | (does not return) |
| 4 | `read` | `sys_read(fd, buf, len)` | Bytes read, `E_FAULT` on bad ptr |
| 5 | `ps` | `sys_ps(buf, size)` | Bytes written, `E_FAULT` on bad ptr |
| 6 | `kill` | `sys_kill(pid)` | 0, `E_BADCALL` if pid out of range |
| 7 | `ticks` | `sys_ticks()` | `timer_get_ticks()` (10 ms granularity) |

Error codes: `E_BADCALL = -1`, `E_FAULT = -2`.

### 8.3 Dispatch Mechanism

```c
void syscall_dispatch(exception_context_t *ctx) {
    uint32_t nr = ctx->r[7];
    if (nr >= NR_SYSCALLS) {
        ctx->r[0] = E_BADCALL;
        return;
    }
    ctx->r[0] = syscall_table[nr](ctx->r[0], ctx->r[1], ctx->r[2], ctx->r[3]);
}
```

`syscall_table[]` là function pointer array. `NR_SYSCALLS = 8`.

### 8.4 Pointer Validation

```c
static int valid_user_ptr(const void *ptr, uint32_t len) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t end  = addr + len;
    if (end < addr)                              return 0;  /* overflow */
    if (addr < USER_VIRT_BASE)                   return 0;
    if (end > USER_VIRT_BASE + USER_REGION_SIZE) return 0;
    return 1;
}
```

Kernel không bao giờ dereference pointer từ user space trực tiếp. Mọi `write(fd, buf, len)`, `read(fd, buf, len)`, `ps(buf, size)` đều gọi `valid_user_ptr()` trước. Nếu không hợp lệ → return `E_FAULT`.

### 8.5 BLOCKED Read Path

```c
int32_t sys_read(int32_t fd, int32_t buf, int32_t len, int32_t unused) {
    if (!valid_user_ptr((void*)buf, len)) return E_FAULT;
    int32_t total = 0;
    while (total == 0) {
        while (!uart_rx_empty() && total < len) {
            ((uint8_t*)buf)[total++] = uart_rx_pop();
        }
        if (total == 0) scheduler_block_on_input();  // BLOCKED
    }
    return total;
}
```

Nếu ring buffer rỗng, process chuyển sang BLOCKED. scheduler chọn process khác chạy. Khi UART RX IRQ fire, handler gọi `scheduler_wake_reader()` → blocked process được đánh thức ở lần schedule kế tiếp.

### 8.6 Tail-End Schedule

Cả `handle_svc()` và `handle_irq()` đều gọi `schedule()` ở cuối. Điều này đảm bảo:

- `sys_yield`: flag resched → schedule() chọn process khác
- `sys_exit`: set DEAD → schedule() skip vĩnh viễn
- `sys_kill`: set target DEAD → schedule() skip target
- Timer IRQ: flag resched → time slice expired

---

## 9. Device Driver Framework

### 9.1 Architecture: Core + Chip Ops

Mỗi subsystem có 3 layers:
1. **Subsystem core** — shared state (ring buffer, tick count, dispatch table), public API
2. **Chip driver** — ops implementation (hardware register access)
3. **Board wire-up** — instantiate device struct, bind ops + base addresses + IRQs

### 9.2 Pattern

```c
// Chip ops interface
struct uart_ops {
    void (*init)(struct uart_device *dev);
    void (*putc)(struct uart_device *dev, char c);
    void (*rx_irq)(struct uart_device *dev);
};

// Device instance
struct uart_device {
    const struct uart_ops *ops;
    uint32_t base;       // MMIO base address
    uint32_t irq;        // Interrupt line
};

// Board wire-up (board.c)
static struct uart_device qemu_uart0 = {
    .ops = &pl011_ops,
    .base = UART0_BASE,
    .irq = IRQ_UART0,
};
```

### 9.3 Three Subsystems

#### UART

| Layer | Files | Role |
|---|---|---|
| Core | `drivers/uart/uart_core.c` | Ring buffer (128 B), `uart_printf`, `uart_putc`, `uart_rx_push/pop` |
| Chip: PL011 | `drivers/uart/pl011.c` | QEMU: 24 MHz UARTCLK, 115200 baud, RX FIFO + RT interrupt |
| Chip: NS16550 | `drivers/uart/ns16550.c` | BBB: 48 MHz input, 115200 baud, RHR interrupt |

Core API: `uart_init()`, `uart_putc(char)`, `uart_printf(fmt,...)`, `uart_print_hex(val)`.

`uart_printf` hỗ trợ: `%c %s %d %u %x %p %% %08x`. `\n` tự động thành `\r\n`.

#### Timer

| Layer | Files | Role |
|---|---|---|
| Core | `drivers/timer/timer_core.c` | Tick count, user_handler callback |
| Chip: SP804 | `drivers/timer/sp804.c` | QEMU: 1 MHz, 32-bit, periodic mode |
| Chip: DMTIMER | `drivers/timer/dmtimer.c` | BBB: 24 MHz CLK_M_OSC, posted mode, OVF IRQ |

Core API: `timer_init(period_us)`, `timer_irq()`, `timer_get_ticks()`, `timer_set_handler()`.

`.timer_tick()`: increment tick counter, fire user_handler (nếu set).

#### Interrupt Controller

| Layer | Files | Role |
|---|---|---|
| Core | `drivers/intc/intc_core.c` | `irq_table[128]`, dispatch loop, registration |
| Chip: GICv1 | `drivers/intc/gicv1.c` | QEMU: distributor + CPU I/F, PMR=0xF0, SPI lines |
| Chip: AM335x INTC | `drivers/intc/am335x_intc.c` | BBB: 4 banks, soft reset, NEWIRQAGR EOI |

Core API: `irq_init()`, `irq_register(irq, handler)`, `irq_enable(irq)`, `irq_dispatch()`.

### 9.4 Platform Wire-Up Example

**QEMU (`board.c`):**
```c
void platform_init_devices(void) {
    uart_set_console(&qemu_uart0);   // PL011
    timer_set_device(&qemu_timer0);  // SP804
    intc_set_device(&qemu_gic);      // GICv1
}
```

**BBB (`board.c`):**
```c
void platform_init_devices(void) {
    clock_enable_timer2();           // AM335x-specific clock gating
    uart_set_console(&bbb_uart0);    // NS16550
    timer_set_device(&bbb_timer2);   // DMTIMER
    intc_set_device(&bbb_intc);      // AM335x INTC
}
```

### 9.5 Platform-Specific Clock Gating (BBB)

Trên BBB, DMTIMER2 clock bị gate bởi CM_PER. `board.c` phải:
1. Disable module qua `CM_PER_TIMER2_CLKCTRL`
2. Select `CLK_M_OSC` (24 MHz) qua `CM_CLKSEL_TIMER2_CLK`
3. Wake L4LS clock domain (`CM_PER_L4LS_CLKSTCTRL = SW_WKUP`)
4. Re-enable module, wait `IDLEST_FUNC`

Việc này phải xảy ra trước `timer_init()`, nên không thể đặt trong chip driver generic.

---

## 10. User Space Architecture

### 10.1 Binary Format

Flat `.bin` files. Position-dependent, linked tại absolute VA `0x40000000`. Kernel copy bytes trực tiếp — không có ELF loader.

**Linker script (`user/linker/user.ld`):**
```
VMA = 0x40000000
.text (crt0.S first) → .rodata → .data → .bss
```

### 10.2 Process Memory Layout (User VA)

```
0x40100000  ┌──────────────────────┐  USER_STACK_TOP
            │ User stack grows down│
            │                      │
0x40000XXX  ├──────────────────────┤
            │ .bss                 │
            │ .data                │
            │ .rodata              │
            │ .text                │
            │   _ustart (crt0.S)   │
0x40000000  └──────────────────────┘  USER_VIRT_BASE
```

1 MB window. Không có guard page, không có W^X separation. Stack mặc định top = 0x40100000, pre-set bởi kernel (`ctx.sp_usr = USER_STACK_TOP`).

### 10.3 CRT0

**File:** `user/crt0.S`

```asm
_ustart:
    bl      main
    mov     r7, #3          @ SYS_EXIT
    svc     #0
```

Entry point `_ustart`, gọi `main()`, nếu return thì `sys_exit`.

### 10.4 Minimal Libc

**File:** `user/libc/ulib.c`, header `user/libc/ulib.h`

| Function | Purpose |
|---|---|
| `ulib_strlen(s)` | String length |
| `ulib_puts(s)` | Output string via `sys_write` |
| `ulib_putu(n)` | Output unsigned decimal |
| `ulib_putc(c)` | Output single char |
| `ulib_strcmp(a, b)` | String compare |
| `ulib_strncmp(a, b, n)` | Bounded string compare |
| `ulib_atoi(s)` | ASCII to integer |
| `ulib_tag(pid, msg)` | Format `[PID] msg` output |

Không có `malloc`, `errno`, `FILE`, `printf`.

**Syscall wrappers (`user/libc/syscall.h`):** inline assembly, mỗi wrapper gọi `svc #0` với r7 = syscall number.

### 10.5 Three User Programs

| Program | PID | Behavior | Demonstrates |
|---|---|---|---|
| **counter** | 0 | In số đếm tăng dần, đo delta bằng `sys_ticks`, yield mỗi ~3 s | Cooperative multitasking, timer |
| **runaway** | 1 | Vòng lặp vô hạn, không yield | Preemption — kernel cưỡng chế CPU |
| **shell** | 2 | Đọc command từ UART, parse + dispatch 6 lệnh | Interactive I/O, blocking read, fault isolation |

**Shell commands:**
| Command | Description |
|---|---|
| `help` | List 6 commands |
| `ps` | Show process table with states |
| `kill <pid>` | Kill a process by PID |
| `echo <text>` | Print back text |
| `clear` | ANSI escape clear screen |
| `crash` | `*NULL = 0xDEADBEEF` → Data Abort (fault isolation demo) |

---

## 11. Context Switch Internals

### 11.1 Function Signature

```c
void context_switch(process_t *prev, process_t *next);
```

Pure assembly (`kernel/arch/arm/proc/context_switch.S`). Called from SVC mode with IRQ masked. `prev=NULL` là special case cho first-time process entry.

### 11.2 Save Side (prev != NULL)

```
push {r4-r11, lr}               @ Save callee-saved regs lên SVC stack
str sp, [prev, #CTX_SP_SVC_OFFSET]  @ Save SVC SP vào PCB

cps #0x1F                       @ Switch to SYS mode (access banked SP_usr, LR_usr)
str sp_usr, [prev, #CTX_SP_USR_OFFSET]
str lr_usr, [prev, #CTX_LR_USR_OFFSET]
cps #0x13                       @ Back to SVC mode
```

### 11.3 Address-Space Switch

```
ldr r0, [next, #PROC_PGD_PA_OFFSET]
orr r0, #0x4A                   @ WB-WA shareable attributes
mcr p15, 0, r0, c2, c0, 0      @ TTBR0 = next->pgd_pa | 0x4A
mov r0, #0
mcr p15, 0, r0, c8, c7, 0      @ TLBIALL
mcr p15, 0, r0, c7, c5, 0      @ ICIALLU
dsb
isb
```

### 11.4 Load Side

```
cps #0x1F                       @ SYS mode
ldr sp_usr, [next, #CTX_SP_USR_OFFSET]
ldr lr_usr, [next, #CTX_LR_USR_OFFSET]
cps #0x13                       @ Back to SVC

ldr sp, [next, #CTX_SP_SVC_OFFSET]  @ Restore SVC stack
pop {r4-r11, lr}                     @ Restore callee-saved
bx lr                                @ Resume
```

### 11.5 NULL-Prev Bootstrap

Khi `prev=NULL`, nhánh save side được skip hoàn toàn (do `cmp r0, #0` + `beq` ở đầu). Load side vẫn chạy đầy đủ:

1. Restore `sp_usr` = `USER_STACK_TOP` (pre-set bởi initial frame builder)
2. Load `sp_svc` từ `next->ctx.sp_svc` — trỏ vào 25-word initial frame
3. `pop {r4-r11, lr}` — lr = `ret_from_first_entry`
4. `bx lr` → `ret_from_first_entry`

`ret_from_first_entry`:
```
ldmfd sp!, {r0-r12, lr}         @ Pop 16-word IRQ-exit frame
rfefd sp!                       @ spsr=0x10, pc=0x40000000
```

Sau `rfefd`, CPU ở USR mode (CPSR=0x10), PC tại `_ustart`, SP_usr tại `USER_STACK_TOP`.

### 11.6 Resume Path for Preempted Processes

Khi process bị preempt bởi timer IRQ, execution path:

```
timer IRQ → handle_irq() → irq_dispatch() → timer_irq() → scheduler_tick()
                                                           → need_reschedule=1
                          → schedule() → context_switch(prev, next)
```

`lr` saved bởi `push {r4-r11, lr}` trỏ vào instruction sau `bl schedule()` trong `handle_irq()`. Khi process được schedule lại, `bx lr` return đến đó, chạy tiếp `rfefd sp!` để về user mode.

Tương tự cho SVC path:
```
svc #0 → handle_svc() → syscall_dispatch() → schedule() → context_switch(prev, next)
```

---

## 12. Design Limits and Invariants

### Current Limits

- **No L2 page tables** — 1 MB granularity only, user section RWX
- **No demand paging** — toàn bộ memory static, không page allocator
- **No fork/exec** — process count cố định (3), tạo tại boot time
- **No W^X** — kernel .text mapped RW, user .text RWX
- **No stack guard** — user 1 MB window không có guard page
- **Single-reader I/O** — `blocked_reader` single slot, không wait queue
- **Single-core** — không SMP, IRQ nesting disabled
- **No dynamic allocation** — `kmalloc`/`free` không tồn tại

### Required Invariants

- `boot_pgd` và mọi `proc_pgd[i]` phải 16 KB aligned
- User PA slot phải 1 MB aligned
- Kernel occupied PA range không được overlap first user PA slot
- User binaries phải fit trong 1 MB
- Platform physical details (address, IRQ, clock) chỉ trong platform profile — shared MMU code không được assume QEMU addresses khi build BBB
- I-cache consistency: sau mọi write vào user PA slot, phải clean D-cache + invalidate I-cache trước khi process chạy
- Pointer từ user space phải được validate qua `valid_user_ptr()` trước khi kernel access
