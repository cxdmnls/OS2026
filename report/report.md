# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab3：断，都可以断（Trap、中断与异常处理） |
| **小组成员** | 2413603-常侯鑫、2411834-张岩、2413404-邓耀飞 |
| **完成日期** | 2026-11-05 |

### 小组分工

代码分工：

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2411834-张岩 | 第一部分：`RESTORE_ALL` 与 `__alltraps`，Trap 上下文恢复和入口调用 |
| 2413603-常侯鑫 | 第二部分：`interrupt_handler()`，中断分类与时钟中断 |
| 2413404-邓耀飞 | 第三部分：`exception_handler()`，异常处理、代码集成与课堂演示 |

报告分工：

- 张岩：整体逻辑线、第一部分实现、对应 Spec/Prompt、Bug 记录及 OS 原理对照。
- 常侯鑫：中断与时钟模块的实现、Prompt 和测试记录。
- 邓耀飞：异常处理、Lab2 依赖迁入、集成测试和课堂演示。

---

## 一、实验目的

本实验的主要目的是：

1. 理解 RISC-V 中 Trap、中断和异常的基本机制，以及硬件从当前执行流转入内核处理程序的流程。
2. 掌握 Trap 发生前后的上下文保存与恢复方法，理解 `trapframe`、内核栈和寄存器布局之间的关系。
3. 完成从汇编入口 `__alltraps` 到 C 语言函数 `trap(struct trapframe *tf)` 的衔接，并通过 `sret` 返回原执行流。
4. 为后续实现中断处理、异常处理、用户态进程和进程调度建立硬件与软件之间的基础接口。
5. 学习使用结构化 Prompt 和 AI 辅助开发、审查和调试操作系统底层代码。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2411834-张岩 | Codex | GPT-5 | 用于汇编代码实现、兼容性检查和报告整理 |
| 2413603-常侯鑫 | 待补充 | 待补充 | 负责中断模块 |
| 2413404-邓耀飞 | Codex | GPT-6 | 负责异常、集成与演示 |

运行及编译环境：

- WSL 下的 GNU/Linux 环境
- `riscv64-unknown-elf-gcc 14.2.0`
- GNU Assembler 2.45.50
- QEMU 4.1.1（`/opt/qemu-4.1.1/bin/qemu-system-riscv64`，内置 OpenSBI v0.4）
- 构建命令：`make`
- 运行命令：`make qemu QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64`
- 评分命令：`PATH=/opt/qemu-4.1.1/bin:$PATH make grade`。评分脚本直接调用 `qemu-system-riscv64`，因此需要设置 PATH。系统默认 QEMU 是 10.2.1，本次测试明确使用 4.1.1。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab3 的核心主线是建立“硬件产生 Trap，进入内核保存现场，由 C 代码分类处理，最后恢复现场并返回原执行流”的完整闭环。

在 Lab2 中，内核已经能够初始化和使用物理内存；但内核还不能可靠响应来自硬件或指令执行过程的突发事件。本实验因此从 RISC-V 的中断机制出发，先建立统一的 Trap 入口和上下文保存区域，再让处理逻辑进入 C 函数，最后通过恢复 `trapframe` 和 `sret` 回到被中断的代码。

整个系统要解决的核心问题是：CPU 在发生 Trap 时只会跳转到 `stvec` 指向的统一入口，并不会自动保存所有通用寄存器；因此操作系统必须自己构造一套稳定的上下文表示，并且保证保存布局、调用约定、恢复顺序和返回状态完全一致。

### 3.2 功能的逐步实现

1. **初始化 Trap 入口**：`idt_init()` 把 `__alltraps` 的地址写入 `stvec`，并将 `sscratch` 清零。这样发生 Trap 后，硬件能够统一跳到汇编入口。
2. **保存 Trap 上下文**：已有的 `SAVE_ALL` 在当前内核栈上分配 `36 * REGBYTES`，按 `struct trapframe` 的布局保存通用寄存器和四个 CSR。`sp` 的原值通过 `sscratch` 中转后保存到 trapframe 的偏移 `2 * REGBYTES`。
3. **连接汇编与 C 处理函数**：`__alltraps` 将当前 `sp` 指向的 trapframe 地址放入 `a0`，调用 `trap(tf)`。这一步必须遵守 RISC-V 调用约定，且不能破坏 trapframe。
4. **分类处理 Trap**：`trap_dispatch()` 根据 `scause` 的最高位区分中断和异常，分别交给 `interrupt_handler()` 与 `exception_handler()`。这两部分由另外两位成员完成。
5. **恢复上下文并返回**：处理函数返回后，`RESTORE_ALL` 从 trapframe 恢复四个 CSR 和通用寄存器，最后由 `sret` 根据 `sepc` 和 `sstatus` 返回原执行流。
6. **集成验证**：接入时钟中断和异常处理后，通过 QEMU、GDB 与评分脚本检查寄存器恢复、异常返回和持续时钟中断，形成完整测试闭环。

这种顺序由依赖关系决定：没有统一的 Trap 入口就无法进入 C 处理函数；没有正确的上下文恢复，即使中断处理成功也无法安全返回；只有先完成基本 Trap 闭环，才能可靠测试中断和异常处理逻辑。

---

## 四、实验内容与实现

### 功能模块一：Trap 上下文恢复与 Trap 入口

**负责人：** 2411834-张岩

#### 模块功能描述

**涉及的核心函数或汇编功能：**

```asm
# kern/trap/trapentry.S
RESTORE_ALL
__alltraps
__trapret
```

```c
// C 语言侧接收上下文的接口
void trap(struct trapframe *tf);
```

`struct trapframe` 由 36 个 `uintptr_t` 组成：偏移 0～31 对应通用寄存器 `x0`～`x31`，偏移 32～35 依次保存 `sstatus`、`sepc`、`badvaddr/stval` 和 `scause`。

`RESTORE_ALL` 是 `SAVE_ALL` 的逆操作。它从当前栈上的 trapframe 恢复四个 CSR 和 31 个可写通用寄存器。`x0` 恒为零，不需要恢复；`x2(sp)` 必须最后恢复，因为其他字段都依赖 trapframe 的基址。

`__alltraps` 是硬件 Trap 和软件处理逻辑之间的桥。它先执行 `SAVE_ALL`，再把 trapframe 地址作为第一个参数传给 `trap()`。`trap()` 返回后，执行流落到 `__trapret`，由 `RESTORE_ALL` 恢复现场并执行 `sret`。

关键实现如下：

```asm
LOAD s0, 32*REGBYTES(sp)
csrw sstatus, s0
LOAD s0, 33*REGBYTES(sp)
csrw sepc, s0
LOAD s0, 34*REGBYTES(sp)
csrw sbadaddr, s0
LOAD s0, 35*REGBYTES(sp)
csrw scause, s0

# 恢复 x1 和 x3～x31
# ...

LOAD x2, 2*REGBYTES(sp)  # 最后恢复 sp

mv a0, sp
jal trap
```

#### Spec 的写法

本模块的 Spec 按“前置条件、后置条件、分支情况和特殊约束”编写：

- **前置条件**：进入恢复流程时，`sp` 必须指向由 `SAVE_ALL` 建立的 `struct trapframe`；其中 36 项内容均已有效保存。
- **后置条件**：四个 CSR、除 `x0` 外的通用寄存器和原 `sp` 均恢复到 Trap 发生时的值；随后 `sret` 可以依据恢复后的 `sepc/sstatus` 返回。
- **正常路径**：先恢复 CSR，再恢复通用寄存器，最后恢复 `sp`。
- **边界和约束**：不能覆盖已有 trapframe；不能提前修改 `sp`；不能改变 `SAVE_ALL`、`struct trapframe` 或 `trap()` 的接口。
- **可验证点**：汇编可以单独生成目标文件；`__alltraps` 和 `__trapret` 符号存在；反汇编显示 `x2` 是最后一个通用寄存器，随后执行 `sret`。

#### 实现迭代过程

本模块实现和验证经历了两次主要迭代。

##### 第一次迭代

**遇到的问题：**

1. 初始代码沿用框架中的旧 CSR 名称 `sbadaddr`。在当前 GNU Assembler 2.45 中，单独汇编 `trapentry.S` 失败：

   ```text
   Error: unknown CSR `sbadaddr'
   ```

2. 审查恢复顺序时发现一个关键风险：如果按照寄存器编号恢复，`x2(sp)` 会在其他字段加载前改变 trapframe 基址，导致后续偏移全部错误。

**问题解决策略：**

- 在 `trapentry.S` 顶部增加兼容定义，将旧的 `sbadaddr` 映射为 RISC-V 新规范中的同名 CSR `stval`。两者都对应 CSR 编号 `0x143`，语义相同，同时保留框架原有注释和写法。
- 明确规定 CSR 和其余通用寄存器恢复完成后才执行 `LOAD x2, 2*REGBYTES(sp)`。
- 用反汇编检查实际指令顺序，确认 `sret` 前的最后一条寄存器恢复指令为 `ld sp, 16(sp)`。

##### 第二次迭代

**验证结果：**

- `trapentry.S` 已能单独编译为目标文件。
- `nm` 显示 `__alltraps` 和 `__trapret` 符号正确生成。
- 反汇编确认执行顺序为：恢复 4 个 CSR → 恢复通用寄存器 → 最后恢复 `sp` → `sret`。
- 完成内存管理依赖迁入和异常处理集成后，全量构建通过；评分中的上下文恢复与断点返回项目得到 35/35。

**关键改进点总结：**

1. 在 `[RELY]` 中明确 trapframe 的 36 项布局和 `sp` 的特殊保存方式。
2. 在 `[SPECIFICATION]` 中规定 `x2(sp)` 必须最后恢复。
3. 针对新工具链增加 `sbadaddr → stval` 的兼容处理，并保持 CSR 语义不变。

---

### 功能模块二：中断与时钟处理

**负责人：** 2413603-常侯鑫

**代码位置：** `code/kern/trap/trap.c`

**核心函数：**

```c
void interrupt_handler(struct trapframe *tf);
```

该模块负责对 `trap_dispatch()` 转发的中断进行分类。重点路径包括：

1. 识别 `IRQ_S_TIMER`，累加 `ticks`，并调用 `clock_set_next_event()` 设置下一次时钟中断。
2. 每经过 `TICK_NUM` 次时钟中断输出一次统计信息，保证时钟中断能够持续触发。
3. 对软件中断输出相应提示；对无法识别的原因调用 `print_trapframe(tf)` 输出完整上下文。

本模块已在 `interrupt_handler()` 中完成中断原因分类。处理函数通过清除 `scause` 最高位取得中断编号。对 supervisor timer，代码先调用 `clock_set_next_event()` 重新装载一次性定时器，再递增 `ticks`；达到 `TICK_NUM` 的整数倍时调用已有的 `print_ticks()`。默认累计输出 10 次统计信息后调用 `sbi_shutdown()`；构建参数可以覆盖 `SHUTDOWN_AFTER`。软件中断会输出提示并清除 `SIP_SSIP`，外部中断输出对应提示，未知中断会输出原因并调用 `print_trapframe()`。

#### 最终提示词

以下提示词按实验指导书规定的四段结构编写，完整记录见 `report/prompt.md`：

````markdown
[PROMPT]
任务：修改 `code/kern/trap/trap.c`，实现 `interrupt_handler(struct trapframe *tf)`，完成 Lab3 必做的中断分类和 supervisor timer 处理。必须修改项目中的实际文件，保留已有 `trap_dispatch()`、`print_trapframe()` 和 `print_ticks()`，只补全 `lab3` 标记对应的功能；旧版 Challenge 不在本任务范围内。
输出要求：以当前工程中的真实宏、结构体和接口为准，直接修改文件并保持公开函数签名不变，不增加与实验无关的功能。

[RELY]
- `struct trapframe` 包含 `cause` 和 `epc`；`tf->cause` 的最高位为 1 表示中断，低位保存中断编号。
- 可用宏：`IRQ_S_SOFT`、`IRQ_S_TIMER`、`IRQ_S_EXT`、`SIP_SSIP`、`TICK_NUM`。
- 可用全局变量和函数：`volatile size_t ticks`、`clock_set_next_event()`、`print_trapframe(tf)`、`print_ticks()`、`sbi_shutdown()`、`clear_csr()`。
- SBI 定时器事件是一次性的，处理一次 timer interrupt 后必须重新设置下一次事件。
- 当前工程未定义 `SHUTDOWN_AFTER`；实现为默认 10 次统计输出后关机（即 1000 ticks），并允许构建参数覆盖该值。

[GUARANTEE]
必须实现：`void interrupt_handler(struct trapframe *tf);`
允许使用本文件已有的辅助函数和宏，不修改 `trap_dispatch()`、函数签名或 Trap frame 布局。

[SPECIFICATION]
Pre-Condition：`tf` 指向当前 Trap frame，调用者已经确认该 Trap 是中断；`tf->cause` 使用 RISC-V 的中断编码。
Post-Condition：中断被分类并完成对应处理，函数返回后可以进入统一的 Trap 恢复流程。
Case 1：`IRQ_S_TIMER` 时设置下一次时钟事件，递增 `ticks`；每达到 `TICK_NUM` 的整数倍调用 `print_ticks()`；统计输出次数达到 `SHUTDOWN_AFTER` 后调用 `sbi_shutdown()`，默认值为 10。
Case 2：`IRQ_S_SOFT` 时输出 supervisor software interrupt，并清除软件中断挂起位。
Case 3：`IRQ_S_EXT` 时输出 supervisor external interrupt 提示。
Case 4：未知原因时输出 `Unknown interrupt: cause <n>` 并调用 `print_trapframe(tf)`。
Requirements：保留中断原因低位解码；timer 事件必须重新装载；不能修改 `tf->epc`；不实现旧版 Challenge。
````

#### 实现与验证记录

- `interrupt_handler()` 已实现 timer 重新装载、时钟计数、周期统计、到达阈值后关机，以及软件、外部和未知中断分支。
- 使用 `make -B obj/kern/trap/trap.o` 单独编译 `trap.c`，编译通过。
- 集成时恢复 Lab2 的内存初始化与 Best-Fit 实现后，全量构建通过。
- QEMU 4.1.1 下连续输出 10 次 `100 ticks` 后正常关机；评分中持续时钟中断项目得到 40/40。

---

### 功能模块三：异常处理

**负责人：** 2413404-邓耀飞

**代码位置：** `code/kern/trap/trap.c`

**核心函数：**

```c
void exception_handler(struct trapframe *tf);
```

该模块负责按照 `scause` 识别并处理断点异常和非法指令异常。处理时需要输出规定信息，并正确更新 `tf->epc`，使 `sret` 返回后不会再次执行触发异常的指令。对于本实验未单独处理的异常，应调用 `print_trapframe(tf)` 进行兜底。

#### 实现逻辑

`exception_handler()` 根据 `tf->cause` 区分非法指令与断点。两者分别输出 `Illegal instruction at 0x...` 和 `Breakpoint at 0x...`，再读取 `tf->epc` 地址处的第一个 16 位半字：最低两位不为 `11` 时按 2 字节推进，否则按 4 字节推进。当前 RV64GC 实验使用 16 位和 32 位指令，不涉及更长指令扩展。

```c
uint16_t instruction = *(const volatile uint16_t *)tf->epc;
tf->epc += (instruction & 3) == 3 ? 4 : 2;
```

读取半字能够兼容仅按 2 字节对齐的压缩指令。这里要求异常指令所在内核地址可读；本实验没有实现用户地址安全读取。其他异常调用 `print_trapframe(tf)`，保留 `epc`。这一分支用于诊断，不负责修复异常；真实的未解决异常在返回后可能再次触发，不能把“打印现场”理解成自动恢复。评分中未知异常使用人工构造的 Trap frame 直接调用处理函数。

`tf->epc` 是栈上保存的返回地址。C 函数修改它后，`RESTORE_ALL` 会把它写回 `sepc`，最终由 `sret` 跳转。中断处理保留返回地址，异常处理只对实验要求跳过的指令推进返回地址。

#### Spec 的写法

- **前置条件**：`tf` 是有效的 Trap frame，调用者已确认该事件为异常；对于断点和非法指令，`epc` 指向可读取的内核指令。
- **后置条件**：断点和非法指令输出对应信息，并跳过该条指令；其余寄存器和状态字段保持不变。
- **断点分支**：识别 `CAUSE_BREAKPOINT`，输出地址，根据实际指令编码推进 `epc`。
- **非法指令分支**：识别 `CAUSE_ILLEGAL_INSTRUCTION`，输出地址，根据实际指令编码推进 `epc`。
- **其他异常**：调用现有的现场打印函数，不擅自跳过未知错误。
- **约束**：保持公开函数签名、Trap frame 布局、已有汇编入口和其他处理函数不变，保留 `lab3` 标记与学号。

#### 集成与调试

初次构建发现 `best_fit_pmm.c` 仅保留框架测试，缺少 `free_area`、分配和释放实现；`pmm.c` 也未补入 `page_init()`。因此从已经完成的 Lab2 分支迁入必要实现，保留 Lab3 新增的保存中断状态、关闭中断、恢复原状态的包装逻辑。默认分配器沿用 Lab3 的 First-Fit，并在临时测试副本中切换 Best-Fit 验证兼容性。

Lab2 使用的 `PHYSICAL_MEMORY_END`、`KERNEL_BEGIN_PADDR` 在 Lab3 未定义，迁入时改用现有宏计算：`KERNTOP - PHYSICAL_MEMORY_OFFSET` 和 `KERNBASE - PHYSICAL_MEMORY_OFFSET`。清理跨分支遗留的目标文件后重新构建，避免使用旧依赖文件。

异常返回验证覆盖压缩断点 `0x9002`、32 位断点 `0x00100073`、16 位非法指令 `0x0000` 和 32 位非法指令 `0x0000007b`。四种指令后均设置成功输出，只有正确返回才能出现对应标记。额外测试在临时副本中注入，不修改评分脚本或正式内核启动流程。

---

## 五、测试与验证

验证日期为 2026-11-05，环境为 WSL Ubuntu、GCC 14.2.0、QEMU 4.1.1。执行命令：

```bash
cd /mnt/d/Desktop/CODE/os/OS2026/code
export PATH=/opt/qemu-4.1.1/bin:$PATH
make clean
make -j2
make qemu
make grade
python3 tools/check_lab3_extra.py
```

| 检查项 | 实际结果 |
|--------|----------|
| 全量构建 | 通过 |
| 默认 First-Fit 的内存自检 | 通过 |
| QEMU 普通启动 | 连续 10 次 `100 ticks`，随后正常关机 |
| Trap 入口、寄存器恢复和断点返回 | 35/35 |
| 非法指令返回和处理函数兜底 | 25/25 |
| 持续时钟中断和重复返回 | 40/40 |
| 总分 | **100/100** |
| 2 字节、4 字节断点返回 | 均通过 |
| 2 字节、4 字节非法指令返回 | 均通过 |
| 临时切换 Best-Fit 后的内存自检和四类异常返回 | 均通过 |

日志：[普通启动](logs/lab3-qemu.txt)、[评分结果摘要](logs/lab3-grade.txt)、[额外集成测试完整输出](logs/lab3-extra.txt)。评分使用原有 `make grade`，未修改测试与评分标准。现有汇编验证日志保留在 `logs/trapentry-verify.txt`。

### 课堂演示步骤

1. 在 VS Code 的 WSL 终端进入 `code`，执行上述 PATH 设置，运行 `qemu-system-riscv64 --version`，确认 4.1.1。
2. 执行 `make clean && make -j2`，展示构建通过。
3. 执行 `make qemu`，指出内存自检成功、时钟初始化、连续统计输出及自动关机。
4. 执行 `make grade`，展示三个项目通过与 `100/100`。该命令会在临时副本中注入异常和上下文测试。
5. 执行 `python3 tools/check_lab3_extra.py`，展示不同指令长度和两种分配器下的返回结果。
6. 讲解 `__alltraps → trap → trap_dispatch → exception_handler → __trapret → sret`，说明修改 `tf->epc` 如何影响最后的返回地址，以及为什么不能提前恢复 `sp`。

普通启动主要展示时钟路径，异常返回由评分和额外测试主动触发。当前 Trap 来自内核 S 模式，`sret` 按 `sstatus.SPP` 返回，不能认为它总是返回 U 模式。

---

## 六、实验总结与收获

### 对操作系统的理解

| 本实验知识点 | 对应 OS 原理 | 关系与差异 |
|-------------|-------------|-----------|
| Trap | 中断、异常与系统调用 | RISC-V 的 Trap 是中断和异常的统称，范围比传统 OS 教材中的 Trap 更广；系统调用、时钟中断和非法指令都从统一入口进入内核。 |
| `trapframe` | 进程上下文、硬件上下文 | `trapframe` 是本实验对一次 Trap 现场的固定内存表示；完整进程上下文还包括页表、打开文件等更多软件状态。 |
| `SAVE_ALL/RESTORE_ALL` | 上下文保存与恢复 | 本实验只恢复寄存器现场，并不执行进程切换；进程调度中的上下文切换还会切换栈、页表和 PCB。 |
| `sstatus`、`sepc`、`scause` | 特权级、程序计数器和中断原因 | 这些 CSR 是硬件和操作系统的接口。软件通过读取或写入它们决定 Trap 原因和返回位置。 |
| `sret` | 中断/异常返回 | `sret` 从 S 模式按保存的 `sepc` 和 `sstatus` 返回，是恢复执行流的关键特权指令。 |
| 内核栈 | 内核态栈管理 | Trap 入口直接在当前内核栈上构造 trapframe，体现内核栈必须可靠、地址可计算。 |

OS 原理中重要但本实验没有完整覆盖的内容：

1. 进程控制块、进程状态转换和多进程调度；本实验还没有创建多个进程。
2. 用户态进程的创建、加载和完整系统调用语义；本实验只处理内核态 Trap，没有创建用户进程。
3. 完整的进程上下文切换、页表切换和地址空间隔离；Lab3 只处理一次 Trap 的寄存器现场。
4. 并发控制、锁、信号量和多核中断分发；当前主要运行单核内核逻辑。
5. 文件系统、设备驱动程序框架、虚拟内存缺页处理和页面置换。

### AI 协作开发的经验

这次实现说明，对底层汇编而言，Prompt 不能只写“实现某某函数”，还必须把数据结构布局、寄存器含义、调用约定和不可修改范围明确写进 `[RELY]` 与 `[SPECIFICATION]`。尤其是 `sp` 恢复顺序和 CPU 工具链名称这类细节，单靠函数名无法推断。

AI 生成的代码仍需人工检查实际反汇编和构建结果。发现 `sbadaddr` 兼容问题后，不能直接忽略编译错误，也不能擅自改变 trapframe 布局，而应确认 CSR 编号和语义后再做兼容处理。最终通过原有评分及额外的指令长度测试确认完整执行流，报告中的测试结论均来自实际运行。

## 参考资料

- [Lab3 实验指导书](http://oslab.mobisys.top/lab2026/_book/lab3/lab3.html)
- [Lab3 练习说明](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_2_1_exercise.html)
- [Lab3 报告要求](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_4_lab_requirement.html)
- [Lab2 报告格式参考](https://github.com/cxdmnls/OS2026/blob/lab2/report/report.md)

---
