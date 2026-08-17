# Address Space Design — Cortex-A-OS Kernel

> Thiết kế không gian địa chỉ của Cortex-A-OS trên ARMv7-A. File này được
> đối chiếu với code hiện tại trong `kernel/include/mmu.h`,
> `kernel/arch/arm/mm/pgtable.c`, `kernel/arch/arm/mm/mmu.c`,
> `kernel/proc/process.c`, `kernel/platform/*/board.h`,
> `kernel/platform/*/periph_map.c`, và linker scripts.
>
> Quy ước quan trọng: phần **kernel core** dùng chung chỉ mô tả VA contract
> và MMU invariant. Mọi chi tiết physical address, peripheral map, IRQ, clock,
> và load address được ghi riêng trong từng platform profile. Không gộp QEMU
> và BBB vào cùng một system map.

---

## 1. Shared Virtual Address Contract

ARMv7-A có 32-bit virtual address space. Cortex-A-OS dùng split kiểu Linux ARM
32-bit: kernel chạy ở high VA, user process chạy ở low VA. Translation table
format hiện tại chỉ dùng **L1 section descriptor** nên granularity cố định là
1 MB; không có L2 page table.

`TTBCR = 0`, nghĩa là `TTBR0` cover toàn bộ 4 GB VA space. Mỗi process có một
L1 table riêng 16 KB, 4096 section descriptors.

> **Constant naming note:** Header `platform.h` define `KERNEL_VIRT_BASE = 0xC0000000`
> cho shared VA contract, còn `mmu.h` define `VA_KERNEL_BASE = 0xC0000000` cho MMU
> subsystem. Cả hai cùng giá trị và phải giữ bằng nhau. Document này dùng
> `KERNEL_VIRT_BASE` khi nói về VA contract và `VA_KERNEL_BASE` khi nói về MMU
> mechanism.

| VA range | Owner | Mapping rule |
| --- | --- | --- |
| `[0x00000000, 0x00100000)` | None | NULL guard, entry 0 = FAULT |
| `[0x00100000, 0x40000000)` | None | Unmapped |
| `[0x40000000, 0x40100000)` | User | Per-process 1 MB user section |
| `[0x40100000, 0xC0000000)` | Mostly none | Unmapped, except platform device sections |
| `[0xC0000000, 0xC0000000 + RAM_SIZE)` | Kernel | High-VA alias of physical RAM |
| `[0xC0000000 + RAM_SIZE, 0x100000000)` | None | Unmapped |

The low 3 GB are the user side of the 3G/1G split, but Cortex-A-OS also installs
some **privileged low-VA device identity mappings** in every page table. Those
device mappings are kernel-only by AP bits; user code cannot access them even
though their VA lies below `0xC0000000`.

Stable logical view after `mmu_drop_identity()`:

```text
0xFFFFFFFF  ┌──────────────────────────────┐
            │ Unmapped                     │
KERN_END    ├══════════════════════════════┤  KERN_END = 0xC0000000 + RAM_SIZE
            │ Kernel RAM high alias        │
            │ kernel .text/.rodata/.data   │
            │ .bss, page tables, stacks    │
            │ embedded user binaries       │
0xC0000000  ├══════════════════════════════┤
            │ Mostly unmapped low VA       │
            │ plus platform device maps    │  kernel-only, XN
0x40100000  ├──────────────────────────────┤  USER_STACK_TOP
            │ User stack grows down        │
            │ User .text/.data/.bss/stack  │
0x40000000  ├══════════════════════════════┤  USER_VIRT_BASE
            │ Unmapped                     │
0x00100000  ├──────────────────────────────┤
            │ NULL guard                   │
0x00000000  └──────────────────────────────┘
```

---

## 2. Section Attributes

All mapped sections use domain 0. `mmu_enable.S` sets `DACR = 0x1`, so domain
0 is **Client** and AP bits are enforced. This is the real protection boundary
between user and kernel mappings.

| Mapping | Descriptor macro | Cache/device behavior | Access | Execute |
| --- | --- | --- | --- | --- |
| NULL/unmapped | `PDE_TYPE_FAULT` | N/A | No access | No |
| Kernel RAM | `PDE_KERNEL_MEM` | Cacheable + bufferable | Kernel RW, user no-access | Yes |
| Device MMIO | `PDE_DEVICE` | Strongly ordered | Kernel RW, user no-access | XN |
| User section | `PDE_USER_TEXT` | Cacheable + bufferable | Kernel RW, user RW | Yes |

Design consequence:

- User code cannot read/write kernel RAM or device MMIO because `PDE_AP_KERN_RW`
  allows privileged access only.
- The 1 MB user section is RWX as a tradeoff of section-only mapping. Cortex-A-OS
  does not yet split user `.text`, `.data`, `.bss`, and stack with L2 pages.
- Kernel `.text` is also mapped RW at section level. This is acceptable for the
  current educational kernel but not a hardened production layout.

---

## 3. Boot Page Table

`mmu_build_boot_pgd()` constructs `boot_pgd` before the MMU is enabled. Because
the kernel is linked at high VA while the CPU is still executing at PA, the
caller passes a PA-view pointer to `boot_pgd`.

Boot PGD entries:

1. Identity-map physical RAM: `[RAM_BASE, RAM_BASE + RAM_SIZE)`.
2. Map high kernel alias: `[0xC0000000, 0xC0000000 + RAM_SIZE)` to
   `[RAM_BASE, RAM_BASE + RAM_SIZE)`.
3. Install platform-specific peripheral identity sections via
   `platform_map_peripherals()`.
4. Leave entry 0 as FAULT for NULL guard.

The identity RAM map exists only for the short transition between
`mmu_enable()` and the high-VA trampoline in `start.S`. `kmain()` later calls
`mmu_drop_identity()`, which clears the RAM identity range and flushes the TLB.
After that, stray PA dereferences fault immediately.

---

## 4. Per-Process Page Tables

Each process owns one 16 KB L1 table in `proc_pgd[NUM_PROCESSES][4096]`.
`pgtable_build_for_proc()` builds a process table by:

1. Zeroing all 4096 entries, so everything defaults to FAULT.
2. Copying all non-zero `boot_pgd` entries except the RAM identity map.
3. Installing one user section:
   `pgd[USER_VIRT_BASE >> 20] = user_pa | PDE_USER_TEXT`.

Resulting invariant:

| VA | Process 0 | Process 1 | Process 2 |
| --- | --- | --- | --- |
| `0x00000000` | FAULT | FAULT | FAULT |
| `0x40000000` | private user PA slot | private user PA slot | private user PA slot |
| `0xC0000000+` | same kernel high alias | same kernel high alias | same kernel high alias |
| Platform MMIO | same kernel-only device map | same kernel-only device map | same kernel-only device map |
| RAM identity PA | not mapped | not mapped | not mapped |

Address-space switch in `context_switch(prev, next)`:

1. Save `prev` callee-saved registers on its SVC stack.
2. Save banked `SP_usr` and `LR_usr`.
3. Write `TTBR0 = next->pgd_pa | 0x4A`.
4. Flush `TLBIALL` and `ICIALLU`, then `DSB; ISB`.
5. Restore `next` banked user registers and SVC stack.

The I-cache invalidate is required because every process executes at the same
user VA `0x40000000`, but that VA maps to a different PA per process.

---

## 5. Boot Address Transition

Common control flow in `kernel/arch/arm/boot/start.S`:

```text
PA mode                                      VA mode
-------                                      -------
1. CPU enters _start at platform PA
2. Compute PHYS_OFFSET = VA(_start) - PA(_start)
3. Set temporary SVC stack at PA
4. Zero BSS through PA addresses
5. mmu_init(PHYS_OFFSET)
   - build boot_pgd through PA pointer
   - enable MMU
6. Identity map keeps PA fetch valid
7. ldr pc, =_start_va  ────────────────► 8. Reset mode stacks at high VA
                                           9. kmain()
                                          10. process_init_all()
                                          11. mmu_drop_identity()
```

After step 11 the kernel never relies on RAM identity mapping. Kernel code and
data are reached through the high alias only.

---

## 6. QEMU System Address Profile

This profile applies only when building with `PLATFORM=qemu`.

Source files:

- `kernel/platform/qemu/board.h`
- `kernel/platform/qemu/board.c`
- `kernel/platform/qemu/periph_map.c`
- `kernel/linker/kernel_qemu.ld`

### QEMU RAM and Load Layout

| Item | Value |
| --- | --- |
| Platform | `QEMU realview-pb-a8` |
| Physical RAM | `[0x70000000, 0x78000000)` = 128 MB |
| Kernel load PA | `0x70100000` |
| Kernel link VA | `0xC0100000` |
| Kernel high alias | `[0xC0000000, 0xC8000000)` -> `[0x70000000, 0x78000000)` |
| `PHYS_OFFSET` | `0x50000000` |

The high alias maps all QEMU RAM from `0xC0000000`, but kernel symbols begin at
`0xC0100000` because the image is loaded at `RAM_BASE + 1 MB`.

### QEMU User PA Slots

| Process | PA slot | Size | User VA |
| --- | --- | --- | --- |
| counter | `[0x70200000, 0x70300000)` | 1 MB | `[0x40000000, 0x40100000)` |
| runaway | `[0x70300000, 0x70400000)` | 1 MB | `[0x40000000, 0x40100000)` |
| shell | `[0x70400000, 0x70500000)` | 1 MB | `[0x40000000, 0x40100000)` |

The kernel image, `.user_binaries`, `.data`, `.bss`, page tables, and stacks
must fit below the first user slot at `0x70200000`.

### QEMU Device Identity Map

These are MMU mapped spans, not only the small register blocks used by each
device. They are section mappings, kernel-only, strongly ordered, and XN.

| Mapped VA/PA span | Size | Devices inside |
| --- | --- | --- |
| `[0x10000000, 0x10200000)` | 2 MB | PL011 UART0 at `0x10009000`, SP804 timer at `0x10011000` |
| `[0x1E000000, 0x1E100000)` | 1 MB | GIC distributor at `0x1E000000`, GIC CPU interface at `0x1E001000` |

QEMU IRQ and clock wiring:

| Device | Driver | IRQ | Clock |
| --- | --- | --- | --- |
| UART0 | PL011 | 44 | board-provided UART clock |
| Timer0 | SP804 | 36 | 1 MHz |
| Interrupt controller | GIC v1 — distributor `0x1E000000`, CPU I/F `0x1E001000` | N/A | N/A |

QEMU stable map after identity drop:

```text
0xFFFFFFFF  ┌──────────────────────────────┐
            │ Unmapped                     │
0xC8000000  ├══════════════════════════════┤
            │ QEMU RAM high alias          │  0xC0000000 -> 0x70000000
            │ kernel symbols start C010... │
0xC0000000  ├══════════════════════════════┤
            │ Unmapped                     │
0x1E100000  ├──────────────────────────────┤
            │ GIC v1 MMIO section          │  identity, kernel-only, XN
0x1E000000  ├──────────────────────────────┤
            │ Unmapped                     │
0x10200000  ├──────────────────────────────┤
            │ PL011 + SP804 MMIO sections  │  identity, kernel-only, XN
0x10000000  ├──────────────────────────────┤
            │ Unmapped                     │
0x40100000  ├──────────────────────────────┤
            │ Current process user section │
0x40000000  ├══════════════════════════════┤
            │ Unmapped / NULL guard        │
0x00000000  └──────────────────────────────┘
```

---

## 7. BeagleBone Black System Address Profile

This profile applies only when building with `PLATFORM=bbb`.

Source files:

- `kernel/platform/bbb/board.h`
- `kernel/platform/bbb/board.c`
- `kernel/platform/bbb/periph_map.c`
- `kernel/linker/kernel_bbb.ld`

### BBB RAM and Load Layout

| Item | Value |
| --- | --- |
| Platform | `BeagleBone Black (AM335x)` |
| Physical RAM | `[0x80000000, 0xA0000000)` = 512 MB |
| Kernel load PA | `0x80000000` |
| Kernel link VA | `0xC0000000` |
| Kernel high alias | `[0xC0000000, 0xE0000000)` -> `[0x80000000, 0xA0000000)` |
| `PHYS_OFFSET` | `0x40000000` |

The BBB SPL copies the kernel image to `0x80000000` and jumps there with the
MMU off. Unlike QEMU, kernel symbols begin at the base of the high alias:
`0xC0000000`.

### BBB User PA Slots

| Process | PA slot | Size | User VA |
| --- | --- | --- | --- |
| counter | `[0x80200000, 0x80300000)` | 1 MB | `[0x40000000, 0x40100000)` |
| runaway | `[0x80300000, 0x80400000)` | 1 MB | `[0x40000000, 0x40100000)` |
| shell | `[0x80400000, 0x80500000)` | 1 MB | `[0x40000000, 0x40100000)` |

The kernel image, `.user_binaries`, `.data`, `.bss`, page tables, and stacks
must fit below the first user slot at `0x80200000`.

### BBB Device Identity Map

These are MMU mapped spans. The register blocks used by the drivers are smaller,
but section-only mapping rounds them up to 1 MB or larger.

| Mapped VA/PA span | Size | Devices inside |
| --- | --- | --- |
| `[0x44E00000, 0x44F00000)` | 1 MB | CM_PER at `0x44E00000`, CM_DPLL at `0x44E00500`, UART0 at `0x44E09000` |
| `[0x48000000, 0x49000000)` | 16 MB | DMTIMER2 at `0x48040000`, AM335x INTC at `0x48200000`, GPIO/L4_PER modules |

BBB IRQ and clock wiring:

| Device | Driver | IRQ | Clock |
| --- | --- | --- | --- |
| UART0 | NS16550/OMAP UART | 72 | board UART clock |
| Timer2 | DMTIMER2 | 68 | 24 MHz `CLK_M_OSC` |
| Interrupt controller | AM335x INTC | N/A | N/A |

`kernel/platform/bbb/board.c` gates DMTIMER2 through CM_PER and selects
`CLK_M_OSC` before the generic timer driver touches DMTIMER2 registers.

BBB stable map after identity drop:

```text
0xFFFFFFFF  ┌──────────────────────────────┐
            │ Unmapped                     │
0xE0000000  ├══════════════════════════════┤
            │ BBB RAM high alias           │  0xC0000000 -> 0x80000000
0xC0000000  ├══════════════════════════════┤
            │ Unmapped                     │
0x49000000  ├──────────────────────────────┤
            │ L4_PER MMIO sections         │  identity, kernel-only, XN
0x48000000  ├──────────────────────────────┤
            │ Unmapped                     │
0x44F00000  ├──────────────────────────────┤
            │ L4_WKUP MMIO section         │  identity, kernel-only, XN
0x44E00000  ├──────────────────────────────┤
            │ Unmapped                     │
0x40100000  ├──────────────────────────────┤
            │ Current process user section │
0x40000000  ├══════════════════════════════┤
            │ Unmapped / NULL guard        │
0x00000000  └──────────────────────────────┘
```

---

## 7.5 QEMU vs BBB Platform Comparison

| Attribute | QEMU (realview-pb-a8) | BBB (AM335x) |
|---|---|---|
| Physical RAM | `[0x70000000, 0x78000000)` = 128 MB | `[0x80000000, 0xA0000000)` = 512 MB |
| Kernel load PA | `0x70100000` | `0x80000000` |
| Kernel link VA | `0xC0100000` | `0xC0000000` |
| `PHYS_OFFSET` | `0x50000000` = `0xC0000000 - 0x70000000` | `0x40000000` = `0xC0000000 - 0x80000000` |
| High alias range | `[0xC0000000, 0xC8000000)` → `[0x70000000, 0x78000000)` | `[0xC0000000, 0xE0000000)` → `[0x80000000, 0xA0000000)` |
| User PA slots | `[0x70200000, 0x70500000)` | `[0x80200000, 0x80500000)` |
| UART base | `0x10009000` (PL011) | `0x44E09000` (NS16550) |
| Timer base | `0x10011000` (SP804, 1 MHz) | `0x48040000` (DMTIMER2, 24 MHz) |
| INTC base | Distributor `0x1E000000`, CPU I/F `0x1E001000` | `0x48200000` (single block) |
| Timer IRQ | 36 | 68 |
| UART IRQ | 44 | 72 |
| Peripheral MMIO | `[0x10000000, 0x10200000)` + `[0x1E000000, 0x1E100000)` | `[0x44E00000, 0x44F00000)` + `[0x48000000, 0x49000000)` |

---

## 8. User-Pointer Boundary

Syscalls validate user buffers with a simple range check:

```c
addr >= USER_VIRT_BASE
addr + len does not overflow
addr + len <= USER_VIRT_BASE + USER_REGION_SIZE
```

This matches the current fixed user address space: a single 1 MB section at
`0x40000000`. Invalid pointers return `E_FAULT`; they should not crash the
kernel. This is the project's simplified version of `copy_from_user()` and
`copy_to_user()`.

> **Blocking read constraint:** `sys_read` uses `scheduler_block_on_input()` which
> has only a single `blocked_reader` slot. Only one process can block on UART I/O
> at a time — currently the shell is the sole consumer. Extending to multiple
> readers would require a wait queue.

---

## 9. Design Limits and Required Invariants

Current deliberate limits:

- No L2 page tables.
- No demand paging, page allocator, `fork`, or `exec`.
- No W^X separation for kernel or user sections.
- No per-thread stack guard inside the user 1 MB section.
- Static process count: `NUM_PROCESSES = 3`.

Invariants that must stay true:

- `boot_pgd` and every `proc_pgd[i]` are 16 KB aligned.
- Every user PA slot is 1 MB aligned.
- Kernel occupied PA range must not overlap the first user PA slot.
- User binaries copied into PA slots must each fit inside 1 MB.
- All kernel-visible device MMIO spans must be installed by the selected
  platform's `platform_map_peripherals()`.
- Platform physical details stay in the selected platform profile; shared MMU
  code must not assume QEMU addresses while building BBB, or vice versa.
- **I-cache consistency:** after any write to a process's user PA slot (via the
  kernel high-VA alias at `VA_KERNEL_BASE + user_phys_base`), the kernel must
  clean the D-cache to the Point of Unification (`DCCMVAU`) and invalidate the
  I-cache (`ICIALLU` + `DSB` + `ISB`) before the process executes. This is
  `icache_sync()` in `process.c`.
