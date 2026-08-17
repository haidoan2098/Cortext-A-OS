# 1. Kiến trúc tổng thể Kernel

> **Mục đích:** Sơ đồ kiến trúc từ userspace xuống hardware —
> các khối chính và luồng tương tác.

```text
+--------------------------------------------------------------------------------+
|                                  USERSPACE                                     |
|                                                                                |
|     +----------------+      +----------------+      +----------------+         |
|     |   Process 0    |      |   Process 1    |      |   Process 2    |         |
|     |   counter      |      |   runaway      |      |    shell       |         |
|     +----------------+      +----------------+      +----------------+         |
|              \                       |                       /                 |
|               \                      |                      /                  |
|                +------------------------------------------------+              |
|                |       Minimal libc + syscall wrappers          |              |
|                +------------------------------------------------+              |
+--------------------------------------------------------------------------------+
                                  |
                                  v
                         svc #0 / ARM EABI
                                  |
+--------------------------------------------------------------------------------+
|                              CORTEX_A_OS KERNEL                                   |
|                                                                                |
|  +----------------------+    +----------------------+    +-------------------+  |
|  |   Syscall Handler    |    |      Scheduler       |    | Process Manager   |  |
|  | write/read/exit      |<-->| round-robin, 10ms    |<-->| PCB, states,      |  |
|  | yield/getpid/ps      |    | preemptive tick      |    | kernel stacks     |  |
|  | kill/ticks           |    +----------------------+    +-------------------+  |
|  +----------+-----------+                |                         |            |
|             |                            |                         |            |
|             v                            v                         v            |
|  +----------------------+    +----------------------+    +-------------------+  |
|  | Exception/IRQ Layer  |    |      MMU/Memory      |    | Context Switch    |  |
|  | VBAR, SVC, IRQ,      |    | 3G/1G split,         |    | save regs, swap   |  |
|  | abort, undef         |    | per-process PGD      |    | TTBR0, flush TLB  |  |
|  +----------+-----------+    +----------+-----------+    +-------------------+  |
|             |                           |                                      |
|             v                           v                                      |
|  +----------------------+    +----------------------+    +-------------------+  |
|  |   Platform Layer     |    |     User Loader      |    |  Kernel Core      |  |
|  | bbb_ops              |    | counter/runaway/     |    | platform-agnostic |  |
|  | board init + IRQ map |    | shell -> 1 MB slots  |    | C + ARM asm       |  |
|  +----------+-----------+    +----------------------+    +-------------------+  |
|             |                                                                  |
|             v                                                                  |
|  +----------------------+    +----------------------+    +-------------------+  |
|  |     UART Driver      |    |     Timer Driver     |    | Interrupt Ctrl    |  |
|  | NS16550              |    | DMTIMER2             |    | AM335x INTC       |  |
|  | console + RX wake    |    | 10ms scheduler tick  |    | IRQ dispatch      |  |
|  +----------------------+    +----------------------+    +-------------------+  |
+--------------------------------------------------------------------------------+
       |                         |                         |              |
       v                         v                         v              v
+--------------------------------------------------------------------------------+
|                                  HARDWARE                                      |
|                                                                                |
|  +----------------+  +----------------+  +----------------+  +---------------+ |
|  | Cortex-A8      |  | DDR/RAM        |  | UART           |  | Timer + INTC  | |
|  | ARMv7-A        |  | 512 MB @ 0x80000000| | serial        |  | IRQ source    | |
|  +----------------+  +----------------+  +----------------+  +---------------+ |
+--------------------------------------------------------------------------------+
```

## Các khối trong kernel

| Khối | Vai trò | Tương tác |
| --- | --- | --- |
| Scheduler | Round-robin, preempt sau 10ms | Timer → schedule → context switch |
| Process Management | 3 PCB, per-process page table + kernel stack | Scheduler đọc state, context switch đọc/ghi ctx |
| Syscall Handler | Dispatch 8 syscall (r7), pointer validation | User `svc #0` → handler → schedule |
| MMU | 1 MB section mapping, per-process L1 table | Context switch swap TTBR0 |
| Exception Handler | C handler cho IRQ/SVC/Data Abort/Undef | IRQ → dispatch → schedule; Fault → kill/panic |
| UART driver | TX polling, RX ring buffer 128 byte | sys_write, sys_read, UART RX IRQ |
| Timer | DMTIMER2, 10ms period | IRQ → scheduler_tick |
| INTC | AM335x INTC, irq_table[128] | Hardware IRQ → dispatch → handler |

Kernel core (scheduler, process, syscall, MMU, exception) platform-agnostic —
chạy trên cả QEMU lẫn BeagleBone Black. Driver subsystem dùng `struct *_ops`
để chip driver thay qua mà core không cần sửa.
