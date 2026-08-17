# Tech Docs — Plan

> Tài liệu kỹ thuật Cortex-A-OS. Mục đích: người đọc hiểu được bản chất OS kernel —
> tại sao nó tồn tại, tại sao phải làm thế, bên dưới C code thật sự xảy ra chuyện gì.
>
> Không phải reference manual. Không phải tutorial làm theo.
> Là tài liệu giải thích bản chất OS kernel thông qua việc tự xây dựng từng thành phần.
>
> **Người đọc target:** dev đã biết C, nhưng chưa biết C thật sự chạy thế nào trên hardware.
> Viết `int x = 5` hàng ngày nhưng không biết nó thành instruction gì. Gọi function
> nhưng không biết stack frame là gì. Dùng pointer nhưng không biết address bus hoạt động ra sao.
> Doc này bắt đầu từ đó.

---

## Nguyên tắc viết

1. **Giải thích từ gốc** — không giả định người đọc biết OS. Bắt đầu từ CPU hoạt động thế nào,
   tại sao cần thứ này, nếu không có thì sao.
2. **Khái niệm là nhãn dán, không phải điểm xuất phát** — không ném thuật ngữ rồi giải thích.
   Dẫn dắt từ vấn đề → giải pháp → rồi chốt "cái đó gọi là X". Người đọc hiểu bản chất trước,
   tên gọi chỉ là nhãn dán lên thứ họ đã hiểu.
   Sai: "MMU dịch virtual address thành physical address."
   Đúng: "...đặt phần cứng giữa CPU và RAM, tra bảng mỗi lần truy cập. Phần cứng đó gọi là **MMU**."
3. **Tại sao trước, như thế nào sau** — mỗi đoạn code phải trả lời được "tại sao viết thế"
   trước khi nói "code này làm gì".
4. **QEMU và BBB viết chung** — nội dung chính viết chung, chỗ nào khác nhau thì ghi rõ
   cả hai platform. Không tách thành 2 docs riêng.
5. **Diagram là bắt buộc, không phải trang trí** — mỗi chapter ít nhất 1 sơ đồ "Đã xây dựng
   đến đâu" (module stack + flow khởi động) và 1–2 diagram trong "Cách hoạt động". Reader
   phải nhìn được bức tranh tổng quan và cơ chế vận hành qua hình, không phải qua chữ.
6. **Diagram inline trong markdown** — không tách file riêng, không link ảnh ngoài.
   Dùng text diagram (ASCII/box-drawing) cho layout tĩnh (memory map, struct, module stack).
   Dùng mermaid cho flow/sequence/state transition. Độ sâu trung bình — reader hiểu trong
   15 giây, không đi vào chi tiết instruction.
7. **Không viết Gotcha riêng** — lỗi thường gặp không phải section độc lập. Nếu thật sự
   non-obvious (insight về platform, trade-off kiến trúc), lồng vào Thiết kế/Implementation.
   Nếu chỉ là chi tiết code → bỏ, reader sẽ gặp khi tự viết.

---

## Cấu trúc mỗi chapter

| Phần | Mục đích |
| --- | --- |
| **Đã xây dựng đến đâu** | Sơ đồ tổng quan system sau chapter này. Module mới trong chapter được highlight (★). Kèm diagram flow khởi động cập nhật. Giúp reader biết mình đang ở đâu trong hành trình xây OS. |
| **Nguyên lý** | Bản chất vấn đề OS — giải thích từ gốc cho người chưa biết gì. CPU hoạt động thế nào, tại sao mọi kernel đều phải giải quyết điều này. Không dùng thuật ngữ chưa giải thích. |
| **Bối cảnh** | CPU đang ở trạng thái nào khi vào chapter này. Cái gì đã có từ chapter trước, cái gì chưa. |
| **Vấn đề** | Lỗ hổng cụ thể nếu không giải quyết ở bước này. Hậu quả thật, không trừu tượng. |
| **Thiết kế** | Quyết định kiến trúc và trade-off. Chọn gì, bỏ gì, tại sao. Có cách nào khác không, tại sao không chọn. |
| **Cách hoạt động** | 1–3 diagram mô tả cơ chế vận hành — lifecycle, mode transition, data flow. Dừng ở mức "cái gì → cái gì → cái gì", không đi vào bit register hay opcode. Mermaid cho flow/sequence, ASCII cho snapshot tĩnh. |
| **Implementation** | Code thật từ project, chỉ giữ phần cốt lõi. Nếu nhiều file/hàm có cùng pattern, show 1 ví dụ rồi nói "các cái còn lại cùng pattern". Link đến file gốc thay vì paste toàn bộ. |
| **Testing** | (Khi áp dụng được) — bảng ngắn: test gì / cách trigger / kết quả. Không paste code test đầy đủ. |
| **Liên kết** | File nào trong code, phụ thuộc chapter nào, chapter tiếp là gì. |

**Nguyên tắc diagrams:**

- **1–3 diagram mỗi chapter**, không hơn — quá nhiều = loãng
- **Độ sâu trung bình** — reader nhìn 15 giây là hiểu flow, không đi vào chi tiết instruction
- **Mermaid** khi có chuỗi sự kiện theo thời gian (sequence, flow có branch, state transition)
- **ASCII text** khi là snapshot tĩnh (memory layout, stack state, module stack)
- **Chọn format rõ hơn** cho từng trường hợp — không ép tất cả vào 1 format

**Không viết Gotcha section** — những chỗ dễ sai lồng vào Thiết kế hoặc Implementation nếu
thật sự non-obvious. Phần lớn gotcha là chi tiết code, không phải insight kiến trúc → bỏ.

---

## Cấu trúc thư mục

```
docs/tech_docs/
├── PLAN.md              ← file này
├── 00_foundation/
├── 01_boot/
├── 02_exceptions/
├── 03_mmu/
├── 04_interrupts/
├── 05_process/          ← PCB + context switch + scheduler
├── 06_syscall/
├── 07_userspace/
└── 08_shell/
```

---

## Nội dung từng chapter

### 00 — Foundation: Từ C đến silicon

> Chapter này không dạy assembly. Nó trả lời một câu hỏi: **C code thật sự chạy thế nào
> trên hardware?** Mỗi phần kết nối trực tiếp vào chapter sau — không có kiến thức nào
> "biết cho vui".

#### Phần 1 — C không tự chạy được

- Người đọc viết `int x = 5` hàng ngày. Nhưng CPU không biết C là gì. Nó chỉ biết đọc
  instruction từ memory, decode, execute. `int x = 5` phải qua compiler để thành thứ CPU hiểu.
- Compiler dịch C thành instruction (mã máy). Mỗi instruction làm 1 việc đơn giản:
  copy số vào register, cộng 2 register, đọc/ghi memory. CPU chạy từng instruction một,
  tuần tự, không biết "chương trình" là gì.
- Register: ô nhớ cực nhỏ nằm ngay trong CPU, truy cập trong 1 cycle. CPU làm mọi phép tính
  trên register, không trên RAM. Muốn tính `a + b`: load a từ RAM vào register, load b,
  cộng, ghi kết quả ngược lại RAM.
- **Vì vậy, ở chapter 01:** bare-metal không có compiler runtime, không có ai setup môi trường
  cho C. Assembly phải tạo ra điều kiện đó trước khi nhảy vào C.

#### Phần 2 — Function call = stack manipulation

- Khi C gọi `foo(a, b)`, CPU cần nhớ: quay về đâu sau khi foo xong? Truyền argument thế nào?
  Biến local của foo để ở đâu?
- Stack giải quyết tất cả. Stack là vùng RAM mà CPU dùng như chồng đĩa: push data lên đỉnh,
  pop ra khi xong. Mỗi function call push 1 "khung" (stack frame) chứa: địa chỉ quay về,
  arguments, biến local. Return = pop khung đó.
- Stack pointer (SP) là register trỏ đến đỉnh stack. Mỗi push: SP giảm (stack mọc ngược),
  ghi data. Mỗi pop: đọc data, SP tăng.
- Không có stack → gọi function đầu tiên → CPU không biết push vào đâu → crash.
  C compiler GIẢI ĐỊNH stack đã có sẵn. Trên Linux, OS setup stack trước khi chạy program.
  Trên bare-metal, không có ai — phải tự setup.
- **Vì vậy, ở chapter 01:** việc đầu tiên của boot code là set SP. Trước khi gọi bất kỳ
  function C nào.

#### Phần 3 — Memory chỉ là bus address

- Khi C viết `*ptr = 42`, CPU gửi address lên bus, ghi 42 vào đó. CPU không biết và không
  quan tâm cái gì ở address đó — RAM, hardware register, hay hư không.
- Nếu address trỏ vào RAM → ghi vào ô nhớ. Bình thường.
- Nếu address trỏ vào UART register → ghi 42 = gửi ký tự ra serial port.
  Cùng 1 instruction C, khác address = khác hoàn toàn hành vi. Đây gọi là MMIO
  (Memory-Mapped I/O) — hardware register nằm trên memory bus, truy cập như RAM.
- Hệ quả: mọi thứ trên bare-metal đều là đọc/ghi address. Bật LED = ghi vào GPIO register.
  Cấu hình timer = ghi vào timer register. Không có API, không có library — chỉ có address.
- **Vì vậy, ở chapter 01 (UART driver) và chapter 04 (timer/INTC):** driver không gì khác ngoài
  đọc/ghi đúng address, đúng giá trị, đúng thứ tự.

#### Phần 4 — CPU mode và privilege

- Nếu mọi code đều truy cập mọi address, thì 1 chương trình lỗi có thể ghi đè kernel,
  disable interrupt, phá hủy toàn bộ system. CPU cần cách phân biệt "code đáng tin" và
  "code không đáng tin".
- ARM giải quyết bằng CPU mode. Mỗi mode có mức đặc quyền khác nhau:
  - **User mode:** ít quyền nhất. Không truy cập hardware register, không disable interrupt.
    Đây là nơi user program chạy.
  - **SVC mode (Supervisor):** toàn quyền. Kernel chạy ở đây.
  - **IRQ mode:** CPU tự chuyển sang khi nhận interrupt.
  - **ABT/UND mode:** CPU tự chuyển sang khi gặp lỗi (truy cập sai, instruction lạ).
  - **FIQ mode:** interrupt nhanh, ưu tiên cao.
- Mỗi mode có register riêng (banked registers): ít nhất SP riêng, một số mode có thêm LR riêng.
  Nghĩa là: khi CPU chuyển từ User sang IRQ mode, SP tự động đổi sang SP_irq — stack khác hoàn toàn.
  Code đang chạy trên User stack không bị ảnh hưởng.
- CPU chuyển mode TỰ ĐỘNG khi exception xảy ra. Code không "xin" chuyển — hardware cưỡng chế.
  Đây là nền tảng bảo mật: user program không thể tự nâng quyền.
- **Vì vậy, ở chapter 01:** boot code setup SP riêng cho từng mode.
  **Chapter 02:** exception handler chạy ở mode tương ứng.
  **Chapter 05/07:** process chạy ở User mode, chỉ vào kernel qua SVC instruction.

#### Liên kết
- Chapter này không có code — chỉ có khái niệm. Mọi thứ ở đây được dùng lại xuyên suốt
  10 chapter còn lại.
- Tiếp theo: Chapter 01 — Boot

---

### 01 — Boot: Từ power-on đến UART output

#### Nguyên lý
- CPU khi mới cấp nguồn không biết gì — nó chỉ biết đọc instruction tại một địa chỉ cố định
  (reset vector). Không có OS, không có stack, không có khái niệm "chương trình".
- Tại sao không nhảy thẳng vào C code: C compiler giả định có stack, có BSS = 0, có interrupt
  được kiểm soát. Bare-metal không có gì — assembly phải tạo ra những điều kiện đó trước.
- Tại sao BBB cần 3 tầng boot (ROM → SPL → Kernel): hardware constraint, không phải design choice.
  ROM chỉ đọc được 109 KB vào SRAM, DDR chưa init.

#### Bối cảnh
- CPU vừa nhận điện. Không có gì ngoài silicon và firmware trong ROM.

#### Vấn đề
- Không có stack → C code crash ngay instruction đầu tiên (push vào đâu?).
- BSS chứa rác → global variable có giá trị ngẫu nhiên → bug âm thầm.
- Interrupt không kiểm soát → IRQ fire khi đang setup stack → crash không debug được.

#### Thiết kế
- Boot sequence: mask IRQ → setup exception stacks → zero BSS → jump C.
  Thứ tự bắt buộc, không đảo được.
- Tại sao 5 exception stacks: ARMv7-A có banked SP per mode. Giải thích từng mode
  (FIQ/IRQ/ABT/UND/SVC) tồn tại để làm gì, khi nào CPU tự chuyển sang mode đó.
- Stack sizes: tại sao SVC 8 KB mà FIQ chỉ 512 B.
- Platform differences: QEMU load ELF trực tiếp vào RAM, BBB cần SPL đọc từ SD card.

#### Implementation
- `start.S` từng block: cpsid, cps + ldr sp, zero BSS loop, bl kmain.
- UART driver: PL011 (QEMU) vs NS16550 (BBB) — khác register set nhưng cùng concept.
- `uart_printf` — tại sao cần ngay từ đầu: không có debugger nào tốt hơn serial output
  khi system chưa ổn định.
- `kmain` boot log: platform, memory layout, CPSR state.

#### Liên kết
- Files: `kernel/arch/arm/boot/start.S`, `kernel/main.c`, `kernel/drivers/uart/`,
  `kernel/include/board.h`, `kernel/linker/kernel_*.ld`
- Tiếp theo: Chapter 02 — Exceptions

---

### 02 — Exceptions: Bắt lỗi thay vì crash mù

#### Nguyên lý
- CPU chỉ biết chạy instruction. Khi gặp tình huống bất thường (địa chỉ sai, instruction lạ,
  hardware cần chú ý), nó không "crash" — nó nhảy đến một địa chỉ cố định gọi là exception vector.
  Nếu ở đó không có code xử lý → CPU chạy vào rác → mới crash thật.
- Exception là cơ chế DUY NHẤT để phần cứng nói chuyện với phần mềm. Timer muốn báo "10ms đã qua"?
  Exception. Process truy cập địa chỉ cấm? Exception. User program muốn gọi kernel? Exception.
  Không có exception → kernel điếc và mù.
- Tại sao gọi là "vector table": mỗi loại exception có một slot cố định trong bảng.
  CPU không cần biết handler ở đâu — nó chỉ nhảy đến slot, slot chứa lệnh nhảy tiếp đến handler.

#### Bối cảnh
- Boot xong, UART hoạt động, nhưng chưa có vector table.
- Nếu Data Abort xảy ra → CPU nhảy đến 0x00000000 (hoặc VBAR default) → không có code → crash mù.

#### Vấn đề
- Debug không thể: không biết crash ở đâu, tại sao, register lúc đó là gì.
- Không thể implement MMU (chapter 03) vì MMU fault = Data Abort = cần handler.
- Không thể implement timer interrupt (chapter 04) vì timer fire = IRQ = cần handler.
- Tóm lại: không có exception handling → không làm được gì tiếp theo.

#### Thiết kế
- ARMv7-A vector table layout: 8 entries, mỗi entry 4 bytes (1 instruction), tại sao đúng 8.
- VBAR register: cho phép đặt vector table ở bất kỳ đâu thay vì 0x00000000.
- Handler strategy cho giai đoạn này: stub handlers — in thông tin debug (PC, LR, CPSR,
  fault address, fault status) ra UART rồi halt. Chưa cần recovery.
- Exception-mode stack là trampoline: tại sao không chạy logic trên exception stack.
  Save vài registers → switch SVC mode → kernel stack → xử lý. Giải thích tại sao.

#### Implementation
- Vector table trong assembly: 8 entry, mỗi entry là `ldr pc, =handler`.
- Data Abort handler: đọc DFAR (fault address), DFSR (fault status), in ra UART.
- Undefined handler: in PC (instruction gây lỗi).
- SVC stub: placeholder cho syscall (chapter 07).
- IRQ stub: placeholder cho timer interrupt (chapter 04).
- Set VBAR: `mcr p15, 0, r0, c12, c0, 0`.

#### Liên kết
- Files: `kernel/arch/arm/exception/vector_table.S`, `kernel/arch/arm/exception/handlers.S`,
  exception handler C functions
- Phụ thuộc: Chapter 01 (stacks phải setup trước)
- Tiếp theo: Chapter 03 — MMU

---

### 03 — MMU: Tách kernel khỏi thế giới

#### Nguyên lý
- CPU chỉ biết truy cập địa chỉ. Khi code viết `*ptr = 42`, CPU gửi địa chỉ lên bus,
  RAM nhận. CPU không biết địa chỉ đó thuộc ai — kernel, process khác, hay hardware.
- Hậu quả: mọi chương trình nhìn thấy toàn bộ RAM. Process lỗi ghi đè kernel → system crash.
  Process ác ý đọc dữ liệu process khác. Kernel không bảo vệ được chính mình.
- MMU đặt giữa CPU và RAM, dịch virtual address → physical address qua page table.
  Kernel kiểm soát page table → kernel quyết định ai thấy gì.
- Page table của process không có entry trỏ vào kernel memory → process không thấy kernel.
  Không phải "bị cấm" mà "không tồn tại" trong thế giới của nó.

#### Bối cảnh
- Exception vector hoạt động → Data Abort handler in được debug info.
  Đây là tiên quyết vì enable MMU sai = Data Abort ngay lập tức.
- Mọi địa chỉ vẫn là PA. Không có isolation.

#### Vấn đề
- Không có isolation: process ghi vào kernel address → corrupt → crash không truy vết.
- Address phụ thuộc hardware: kernel compile cho BBB không chạy trên QEMU (khác RAM base).
- Không có NULL protection: `*(int*)0 = 1` ghi thành công thay vì fault.
- Không thể implement multi-process: mỗi process cần address space riêng.

#### Thiết kế
- Section mapping (1 MB) vs page mapping (4 KB): tại sao chọn section cho 3 process static.
- Virtual address layout: NULL guard, user 0x40000000, kernel 0xC0000000, high vectors 0xFFFF0000.
- 3G/1G split — tại sao theo convention Linux ARM.
- Boot transition: con gà quả trứng — enable MMU nhưng PC đang ở PA.
  Identity map tạm thời → trampoline → xóa identity map.
- Per-process page table: kernel region giống nhau, user region khác nhau. Context switch = swap TTBR0.

#### Implementation
- Build boot page table: identity map + kernel map + peripheral map.
- Enable MMU sequence: DSB → load TTBR0 → set DACR → set SCTLR.M → ISB.
- Trampoline: ldr pc vào VA cao → xóa identity map → flush TLB.
- Reload stack pointers sang VA.

#### Liên kết
- Files: `kernel/arch/arm/mmu/`, `kernel/include/board.h` (address constants)
- Phụ thuộc: Chapter 02 (exception handler phải có trước để debug MMU fault)
- Tiếp theo: Chapter 04 — Interrupts

---

### 04 — Interrupts: Hardware nói chuyện với kernel

#### Nguyên lý
- CPU chạy instruction tuần tự. Nếu không có cơ chế nào khác, CPU sẽ chạy một chương trình
  mãi mãi — không bao giờ dừng để kiểm tra "timer đã đến chưa" hay "UART có data chưa".
- Giải pháp ngây thơ: polling — CPU liên tục hỏi "có gì mới không?" trong vòng lặp.
  Hoạt động nhưng lãng phí 100% CPU time cho việc hỏi.
- Giải pháp thật: interrupt — hardware TỰ báo cho CPU khi có sự kiện. CPU đang làm gì thì
  dừng, nhảy vào handler, xử lý xong quay lại. Giống chuông cửa thay vì ra ngó mỗi 5 giây.
- Interrupt controller (INTC) là bộ phận thu thập tín hiệu từ nhiều nguồn (timer, UART, GPIO...)
  và chuyển thành 1 đường IRQ duy nhất vào CPU. CPU chỉ có 1 chân IRQ — INTC multiplexing.
- Timer interrupt là nền tảng của preemptive scheduler: mỗi 10ms timer fire → kernel lấy lại
  CPU từ process đang chạy → quyết định ai chạy tiếp. Không có timer interrupt = cooperative
  scheduling only = process không yield thì kernel bất lực.

#### Bối cảnh
- MMU bật, kernel chạy ở VA, exception handler hoạt động.
- IRQ stub trong vector table chưa làm gì ngoài halt.

#### Vấn đề
- Kernel không có cách lấy lại CPU từ process đang chạy.
- Không có timer tick → scheduler không hoạt động → không có multitasking thật.

#### Thiết kế
- AM335x INTC vs QEMU PL190 VIC: khác register set, cùng concept.
- Timer selection: DMTimer2 (BBB) vs SP804 (QEMU). Tại sao chọn timer này.
- IRQ handler flow: CPU vào IRQ mode → trampoline (save scratch regs lên IRQ stack) →
  switch SVC → kernel stack per-process → đọc INTC biết source → dispatch → EOI → return.
- Tại sao không re-enable IRQ trong IRQ handler: single-core, shared IRQ stack → nested IRQ
  = stack overflow.

#### Implementation
- INTC driver: init, enable line, clear, EOI.
- Timer driver: config period, start, stop, clear interrupt.
- IRQ handler assembly: trampoline pattern.
- Timer callback: set flag, chưa reschedule (chapter 06 mới dùng).

#### Liên kết
- Files: `kernel/drivers/timer/`, `kernel/drivers/intc/`, `kernel/arch/arm/exception/`
- Phụ thuộc: Chapter 03 (MMU bật, peripheral mapped)
- Tiếp theo: Chapter 05 — Process

---

### 05 — Process: Tạo ảo giác nhiều chương trình

#### Nguyên lý
- CPU chỉ có 1 bộ registers, 1 PC — tại bất kỳ thời điểm nào, nó chỉ chạy 1 dòng code.
  Nó không có khái niệm "process" hay "chương trình". Vậy làm sao tạo ra ảo giác
  nhiều chương trình chạy đồng thời?
- Bằng cách lưu trữ và hoán đổi. Mỗi "process" chỉ là một struct chứa snapshot toàn bộ
  trạng thái CPU (r0-r12, SP, LR, PC, CPSR). Khi muốn chuyển process:
  save registers hiện tại vào struct A → load registers từ struct B → CPU tiếp tục chạy
  như thể nó luôn chạy B. Struct đó gọi là PCB (Process Control Block). Hành động swap
  gọi là context switch.
- "Process" là khái niệm phần mềm — hardware không biết nó tồn tại. Với CPU, nó chỉ
  thấy registers thay đổi giá trị. Phần mềm tạo ra abstraction.

#### Bối cảnh
- MMU bật, per-process page table sẵn sàng.
- Timer interrupt hoạt động — kernel có thể lấy lại CPU mỗi 10ms.
- Chưa có process nào. Kernel chạy trực tiếp trong kmain.

#### Vấn đề
- Chỉ chạy được 1 chương trình. Muốn chạy 3 (counter, runaway, shell) cùng lúc.
- Cần cấu trúc dữ liệu lưu trạng thái mỗi chương trình.
- Cần cơ chế swap trạng thái — phải viết bằng assembly vì C không access được banked registers.

#### Thiết kế
- PCB structure: saved registers, page table pointer, kernel stack pointer, state, pid.
- Process states: READY → RUNNING → READY (round-robin), RUNNING → BLOCKED (chờ I/O),
  RUNNING → DEAD (exit). Tại sao cần BLOCKED: nếu không, process chờ UART phải polling →
  burn 100% time slice → các process khác bị đói.
- 3 process static, không dynamic creation: PCB array cố định, không cần allocator.
- User mode vs SVC mode: process chạy ở User mode (không thể truy cập kernel memory,
  không thể disable interrupt). Kernel chạy ở SVC mode. Ranh giới enforce bởi MMU + CPU mode.
- Kernel stack per-process: tại sao mỗi process cần kernel stack riêng — khi process A
  đang trong syscall handler (trên kernel stack A), timer fire, switch sang B, B cũng syscall →
  nếu chung stack → corrupt. Mỗi process cần stack riêng trong kernel space.

#### Implementation
- PCB struct definition.
- process_create(): setup PCB, page table, initial register values (PC = entry point,
  SP = user stack top, CPSR = User mode).
- context_switch.S: save r0-r12 + LR + SPSR vào PCB A → swap TTBR0 → flush TLB →
  restore r0-r12 + LR + SPSR từ PCB B → movs pc, lr (return + restore CPSR).
- Process init trong kmain: tạo 3 process, nhảy vào process đầu tiên.

#### Liên kết
- Files: `kernel/process/`, `kernel/arch/arm/context_switch.S`
- Phụ thuộc: Chapter 03 (page table), Chapter 04 (timer interrupt)
- Tiếp theo: Chapter 06 — Scheduler

---

### 06 — Scheduler: Ai chạy tiếp?

#### Nguyên lý
- Có 3 process nhưng 1 CPU. Ai quyết định process nào được chạy? Đó là scheduler.
- Nếu scheduler chỉ chạy khi process tự nguyện nhường (yield) → cooperative scheduling.
  Vấn đề: 1 process không yield → các process khác chết đói. Process lỗi (vòng lặp vô hạn)
  → system đứng.
- Preemptive scheduling: timer interrupt cưỡng chế lấy CPU mỗi 10ms. Process KHÔNG CÓ
  QUYỀN từ chối. Kernel luôn giữ quyền kiểm soát.
- Round-robin: đơn giản nhất — chạy lần lượt, mỗi process được 1 time slice (10ms).
  Không priority, không aging. Đủ cho 3 process.

#### Bối cảnh
- 3 process đã tạo, context switch hoạt động.
- Timer fire mỗi 10ms, nhưng chưa có logic chọn process tiếp theo.

#### Vấn đề
- Không có scheduler → context switch không biết switch sang ai.
- Cần skip process BLOCKED và DEAD.
- Cần đảm bảo scheduler chính nó không bị interrupt (atomic).

#### Thiết kế
- Round-robin: duyệt PCB array, tìm process READY tiếp theo, switch.
- Timer handler set `need_reschedule` flag, actual switch xảy ra khi return từ IRQ.
  Tại sao không switch ngay trong IRQ handler: đang trên IRQ stack, chưa save đủ context.
- Time slice = 10ms = 1 timer tick. Đơn giản, không variable quantum.

#### Implementation
- scheduler_tick(): gọi từ timer IRQ, set flag.
- schedule(): chọn next READY process, gọi context_switch().
- Tích hợp vào IRQ return path: check flag → schedule() → return to new process.

#### Liên kết
- Files: `kernel/sched/`
- Phụ thuộc: Chapter 05 (process + context switch)
- Tiếp theo: Chapter 07 — Syscall

---

### 07 — Syscall: Cửa duy nhất vào kernel

#### Nguyên lý
- Process chạy ở User mode — CPU cấm nó truy cập kernel memory, cấm nó disable interrupt,
  cấm nó chạm hardware registers. Nhưng process CẦN kernel làm việc cho nó: in ký tự ra UART,
  đọc input, thoát chương trình.
- Làm sao giao tiếp khi bị cách ly hoàn toàn? Qua một cửa duy nhất: instruction `SVC #0`.
  Khi CPU gặp SVC, nó tự động chuyển sang SVC mode (kernel mode), nhảy vào vector table.
  Kernel kiểm tra process muốn gì (qua r0-r3), thực hiện, trả kết quả, return về User mode.
- Đây là ranh giới tin cậy: kernel KHÔNG TIN process. Mọi pointer từ user space phải validate
  trước khi dùng. Mọi syscall number phải kiểm tra hợp lệ. Process truyền pointer trỏ vào
  kernel memory → kernel từ chối, không crash.

#### Bối cảnh
- Process chạy ở User mode, scheduler hoạt động, MMU enforce isolation.
- Process không có cách nào giao tiếp với kernel — chưa có SVC handler.

#### Vấn đề
- Process muốn in ra UART → không truy cập được UART registers (kernel memory).
- Process muốn thoát → không có cách báo kernel.
- Không có syscall → process hoàn toàn bị cô lập, không làm được gì hữu ích.

#### Thiết kế
- Syscall convention: r7 = syscall number, r0-r3 = arguments, r0 = return value (theo ARM EABI).
- Syscall table: array function pointers, index bằng syscall number.
- Pointer validation: user pointer phải nằm trong 0x40000000–0x40FFFFFF.
  Pointer trỏ vào kernel space → return lỗi, không deref.
- Danh sách syscall: write, read, exit, yield, getpid, ps, kill.

#### Implementation
- SVC handler assembly: save context → extract syscall number → call C dispatcher.
- Syscall dispatcher: validate number, call handler function, return value trong r0.
- sys_write(): validate buffer pointer → uart_puts().
- sys_exit(): set state = DEAD → schedule().
- sys_yield(): set state = READY → schedule().
- User-side wrapper: inline asm, `svc #0`, `mov r7, #SYS_WRITE`.

#### Liên kết
- Files: `kernel/syscall/`, `kernel/arch/arm/exception/` (SVC handler)
- Phụ thuộc: Chapter 05 (process), Chapter 06 (scheduler cho yield/exit)
- Tiếp theo: Chapter 08 — Userspace

---

### 08 — Userspace: Chương trình đầu tiên bên ngoài kernel

#### Nguyên lý
- Kernel quản lý tài nguyên, nhưng nó không phải thứ người dùng tương tác.
  Người dùng tương tác với process — chương trình chạy ở User mode.
- User process không thể dùng bất kỳ thứ gì của kernel trực tiếp: không printf,
  không malloc, không exit(). Mọi thứ phải đi qua syscall.
- Trên Linux, libc (glibc, musl) wrap syscall thành API dễ dùng. Trên Cortex-A-OS,
  tự viết minimal libc — chỉ cần những gì 3 process dùng.
- crt0 (C runtime zero): code chạy TRƯỚC main(). Setup stack, gọi main(), khi main()
  return thì gọi exit(). Trên bare-metal, tự viết.

#### Bối cảnh
- Syscall interface hoạt động. Kernel có thể tạo process và switch giữa chúng.
- Chưa có chương trình nào chạy ở User mode. Process hiện tại là placeholder.

#### Vấn đề
- Cần code chạy ở User mode để chứng minh toàn bộ system hoạt động.
- User code không có printf, strlen, memcpy — phải tự viết.
- User code cần entry point đúng format (crt0 → main → exit).

#### Thiết kế
- crt0.S: setup user stack → bl main → mov r7, #SYS_EXIT → svc #0.
- Syscall wrappers: write(), read(), exit(), yield(), getpid() — thin C functions.
- Minimal libc: strlen, strcmp, memcpy, memset — chỉ những gì dùng thật.
- 3 user programs: counter (in số + yield), runaway (`while(1)` không yield — test preemption), shell (nhận lệnh).
- Link riêng mỗi process — mỗi process là 1 binary riêng, kernel load vào vùng PA riêng.

#### Implementation
- crt0.S.
- Syscall wrappers (inline asm hoặc naked functions).
- Minimal libc functions.
- counter.c: vòng lặp in số + yield.
- Build: user/ Makefile riêng, output binary riêng per process.

#### Liên kết
- Files: `user/`, `user/lib/`, `user/programs/`
- Phụ thuộc: Chapter 07 (syscall interface)
- Tiếp theo: Chapter 09 — Shell

---

### 09 — Shell: Kết nối mọi thứ lại

#### Nguyên lý
- OS không có giá trị nếu người dùng không tương tác được. Shell là giao diện giữa
  con người và kernel — nhận lệnh text, parse, gọi syscall, hiển thị kết quả.
- Trên Unix/Linux gốc, shell chạy qua serial terminal — đúng paradigm Cortex-A-OS đang dùng.
  Người dùng kết nối qua UART, gõ lệnh, nhận output.
- Shell là 1 user process bình thường — không có đặc quyền đặc biệt. Nó dùng cùng syscall
  interface như mọi process khác. Điều này chứng minh: syscall layer đủ mạnh để xây ứng dụng
  tương tác phía trên.

#### Bối cảnh
- Toàn bộ system hoạt động: boot, MMU, interrupt, scheduler, syscall, user processes.
- counter + runaway chạy, nhưng chưa có cách tương tác realtime.

#### Vấn đề
- Người dùng không kiểm soát được gì — chỉ nhìn output chạy qua.
- Muốn xem process list, kill process, xem memory — cần giao diện.

#### Thiết kế
- Shell = process 2, chạy ở User mode.
- Blocking read: gọi sys_read() → process BLOCKED đến khi UART nhận ký tự →
  IRQ handler đánh thức → process READY → scheduler pick → read() return.
  Đây là demo đẹp nhất của BLOCKED state.
- Commands: help, ps, kill `<pid>`, echo `<text>`, clear, crash (test fault isolation).
- Mỗi command = 1 hoặc nhiều syscall. Shell parse string → gọi syscall → in kết quả.

#### Implementation
- shell.c: prompt loop, read line, parse command, dispatch.
- String parsing: strcmp command name, atoi cho pid argument.
- Tích hợp sys_read() blocking: UART RX interrupt → wake shell process.

#### Liên kết
- Files: `user/programs/shell.c`
- Phụ thuộc: Tất cả chapter trước
- Đây là chapter cuối — tổng hợp toàn bộ system
