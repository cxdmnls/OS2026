# Lab3 Prompt 记录

## 张岩：Trap 上下文恢复与 Trap 入口

### 我的设计思路

1. `SAVE_ALL` 已经定义了实际使用的 trapframe，因此不能根据 `struct trapframe` 的字段名重新猜测一套布局；恢复逻辑必须逐项遵守已有保存偏移。
2. `RESTORE_ALL` 是保存过程的逆过程。四个 CSR 和普通寄存器都需要从 trapframe 取得，但 `x0` 是只读零，`x2(sp)` 会改变后续所有加载所依赖的基址，所以 `sp` 必须最后恢复。
3. `__alltraps` 的职责不是处理中断本身，而是建立“硬件入口—汇编上下文—C 函数”的边界。按照 RISC-V 调用约定，trapframe 地址应放入 `a0`，然后调用 `trap(tf)`。
4. `trap()` 正常返回后必须落到 `__trapret`。只有 restore 完成后才可以执行 `sret`，否则返回地址、特权级和寄存器现场都可能错误。
5. 当前框架沿用旧 CSR 名称 `sbadaddr`，而新版工具链使用 `stval`。兼容处理必须保持 CSR 编号和语义不变，不能因此改变 trapframe 布局。

````markdown
[PROMPT]
**任务**：修改 `code/kern/trap/trapentry.S`，完成 Trap 上下文恢复与 Trap 入口功能。

**操作要求**：必须在项目中修改实际文件，而不是只输出代码片段。保留已有 `SAVE_ALL`、`struct trapframe`、`trap()` 函数签名及其他无关代码，只在两个 `lab3 function1` 标记处补全 `RESTORE_ALL` 和 `__alltraps`；保留 `lab3 function1` 注释，并将 `YOUR CODE` 替换为学号 `2411834`。不要引入进程切换、用户态页表或新的 Trap 数据布局。

**输出要求**：以当前工程的真实宏、寄存器名称和结构体布局为准，直接修改文件。实现应能形成“Trap 进入 → SAVE_ALL → 调用 trap(tf) → RESTORE_ALL → sret”的完整闭环。

[RELY]
// 数据结构
`struct trapframe` 由连续 36 个 `uintptr_t` 构成：
- 偏移 0～31：通用寄存器 `x0`～`x31`。
- 偏移 32：`sstatus`。
- 偏移 33：`sepc`。
- 偏移 34：`sbadaddr`（新版 CSR 名称为 `stval`）。
- 偏移 35：`scause`。

// 已有保存关系
- `SAVE_ALL` 将 `sp` 减少 `36 * REGBYTES`，在当前内核栈上建立 trapframe。
- 原 `sp` 先写入 `sscratch`，随后保存到偏移 `2 * REGBYTES`。
- `x0` 保存到偏移 0；`x1` 保存到偏移 1；`x3`～`x31` 分别保存到同号偏移。
- 进入 C 函数前，trapframe 的起始地址就是当前 `sp`。

// 宏定义和工具链
- `REGBYTES`、`STORE`、`LOAD` 定义于 `libs/riscv.h`。
- `sstatus`、`sepc`、`sbadaddr/stval`、`scause` 都是 Supervisor CSR。
- 当前 GNU Assembler 2.45 不再接受旧名称 `sbadaddr`；它与新名称 `stval` 对应同一个 CSR（编号 `0x143`）。

// 可用接口
```c
void trap(struct trapframe *tf);
```

// 调用约定和控制流
- RISC-V 第一个整数参数寄存器为 `a0`。
- `jal trap` 返回后，执行流继续到紧邻的下一条指令，该位置应进入 `__trapret`。
- `__trapret` 在 `RESTORE_ALL` 后执行 `sret`。

[GUARANTEE]
必须完成：
```asm
RESTORE_ALL
__alltraps
```

允许新增：
- `RESTORE_ALL` 内部使用的必要临时寄存器操作。
- 为兼容新旧 CSR 名称所需的预处理器定义。

不得修改：
- `SAVE_ALL` 的保存布局。
- `struct trapframe` 的字段和顺序。
- `void trap(struct trapframe *tf)` 的函数签名。
- `__trapret` 和 `sret` 的返回职责。

[SPECIFICATION]
## RESTORE_ALL

**Pre-Condition**：
- `sp` 指向由 `SAVE_ALL` 创建的完整 trapframe。
- trapframe 中偏移 0～35 的内容均已有效保存。
- 执行恢复时尚未进入 `sret`，当前仍处于 Supervisor 模式。

**Post-Condition**：
- `sstatus`、`sepc`、`sbadaddr/stval`、`scause` 恢复到 trapframe 中保存的值。
- 除只读的 `x0` 外，所有通用寄存器恢复到 Trap 发生前的值。
- `sp` 恢复到 trapframe 中偏移 2 所保存的原值。
- 后续执行 `sret` 时，CPU 能依据恢复后的 `sepc` 和 `sstatus` 返回正确执行流。

**Case 1：正常恢复**
- 从 trapframe 对应的偏移加载四个 CSR。
- 恢复 `x1`、`x3`～`x31`。
- 最后恢复 `x2(sp)`，避免提前改变 trapframe 基址。

**Case 2：恢复零寄存器**
- `x0` 恒为零，不执行不可靠的显式恢复；它的 trapframe 槽位仍保留由 `SAVE_ALL` 写入的零值。

**Case 3：工具链 CSR 名称差异**
- 保持框架原有 `sbadaddr` 写法时，应通过兼容定义映射到当前汇编器接受的 `stval`。
- 该处理不得改变 CSR 编号、恢复顺序或 trapframe 偏移。

**Requirements**：
- 恢复顺序必须保证加载其他字段时 `sp` 仍指向 trapframe 基址。
- 不得覆盖 trapframe 以外的栈数据。
- 不得改变 `SAVE_ALL` 建立的 36 项布局。

## __alltraps

**Pre-Condition**：
- 硬件已因中断或异常跳转到 `stvec` 指向的 `__alltraps`。
- 当前只能依赖硬件保存的 CSR 和当前栈指针，不能假设任何处理函数已经执行。

**Post-Condition**：
- `SAVE_ALL` 已构造完整 trapframe。
- trapframe 地址按照 RISC-V 调用约定作为第一个参数传给 `trap(tf)`。
- `trap(tf)` 返回后执行流进入 `__trapret`，随后完成 `RESTORE_ALL` 并执行 `sret`。

**Case 1：正常 Trap**
- 先保存现场，再把 trapframe 起始地址放入 `a0`。
- 调用 C 函数 `trap`，并在其返回后进入统一恢复路径。

**Case 2：C 处理函数返回**
- 不重复构造新的 trapframe。
- 不修改 `trap()` 返回前的寄存器现场，由 `RESTORE_ALL` 统一完成恢复。

**Requirements**：
- `__alltraps` 必须保持 4 字节对齐。
- `SAVE_ALL` 与 `RESTORE_ALL` 必须使用同一套偏移定义。
- `jal trap` 返回后应直接衔接 `__trapret`。
- 本节只实现底层入口和恢复，不处理中断分类、时钟计数或异常返回地址修正。
````

## 常侯鑫：中断与时钟处理

[PROMPT]

**任务**：修改 `code/kern/trap/trap.c`，实现 `interrupt_handler(struct trapframe *tf)`，完成 Lab3 必做的中断分类和 supervisor timer 处理。必须修改项目中的实际文件，保留已有 `trap_dispatch()`、`print_trapframe()` 和 `print_ticks()`，只补全 `lab3` 标记对应的功能；旧版 Challenge 不在本任务范围内。

**输出要求**：以当前工程中的真实宏、结构体和接口为准，直接修改文件并保持公开函数签名不变，不增加与实验无关的功能。

[RELY]

- `struct trapframe` 包含 `cause` 和 `epc`；`tf->cause` 的最高位为 1 表示中断，低位保存中断编号。
- 可用宏：`IRQ_S_SOFT`、`IRQ_S_TIMER`、`IRQ_S_EXT`、`SIP_SSIP`、`TICK_NUM`。
- 可用全局变量和函数：`volatile size_t ticks`、`clock_set_next_event()`、`print_trapframe(tf)`、`print_ticks()`、`sbi_shutdown()`、`clear_csr()`。
- SBI 定时器事件是一次性的，处理一次 timer interrupt 后必须重新设置下一次事件。
- 当前工程未定义 `SHUTDOWN_AFTER`；实现为默认 10 次统计输出后关机（即 1000 ticks），并允许构建参数覆盖该值。

[GUARANTEE]

必须实现：

```c
void interrupt_handler(struct trapframe *tf);
```

允许使用本文件已有的辅助函数和宏，不修改 `trap_dispatch()`、函数签名或 Trap frame 布局。

[SPECIFICATION]

## interrupt_handler

**Pre-Condition**：`tf` 指向当前 Trap frame，调用者已经确认该 Trap 是中断；`tf->cause` 使用 RISC-V 的中断编码。

**Post-Condition**：中断被分类并完成对应处理，函数返回后可以进入统一的 Trap 恢复流程。

**Case 1**：原因是 `IRQ_S_TIMER` 时，设置下一次时钟事件，递增 `ticks`；每达到 `TICK_NUM` 的整数倍调用 `print_ticks()`；统计输出次数达到 `SHUTDOWN_AFTER` 后调用 `sbi_shutdown()`。若构建时未覆盖该宏，其默认值为 10。

**Case 2**：原因是 `IRQ_S_SOFT` 时输出 supervisor software interrupt，并清除软件中断挂起位。

**Case 3**：原因是 `IRQ_S_EXT` 时输出 supervisor external interrupt 提示。

**Case 4**：原因未知时输出 `Unknown interrupt: cause <n>`，并调用 `print_trapframe(tf)`，不能陷入循环。

**Requirements**：必须保留中断原因低位的解码方式；timer 事件必须重新装载；不能修改 `tf->epc`；不能实现旧版 Challenge。

## 邓耀飞：异常处理

[PROMPT]

**任务**：修改 `code/kern/trap/trap.c`，实现 `exception_handler(struct trapframe *tf)`，完成 Lab3 要求的非法指令异常、断点异常和其他异常的处理。必须修改项目中的实际文件，保留已有 `trap_dispatch()`、`print_trapframe()` 和 `trap()`，只补全 `lab3` 标记对应的功能。

**输出要求**：以当前工程中的真实宏、结构体和接口为准，直接修改文件并保持公开函数签名不变，不修改 `struct trapframe`、Trap 入口汇编和其他处理函数，不增加与实验无关的功能。

[RELY]

- `struct trapframe` 包含 `cause`、`epc` 和通用寄存器保存区；`tf->cause` 保存异常原因，`tf->epc` 保存触发异常的指令地址。
- 可用宏：`CAUSE_ILLEGAL_INSTRUCTION`、`CAUSE_BREAKPOINT`。
- 可用函数：`print_trapframe(tf)`、`cprintf()`；`RESTORE_ALL` 会把修改后的 `tf->epc` 写回 `sepc`，再由 `sret` 返回。
- RISC-V C 扩展允许 16 位压缩指令；指令低两位不为 `11` 时长度为 2 字节，否则本实验按 4 字节指令处理。
- 断点和非法指令属于同步异常，返回时应跳过已经处理的异常指令；普通中断的 `tf->epc` 不由本函数修改。

[GUARANTEE]

必须实现：

```c
void exception_handler(struct trapframe *tf);
```

允许使用本文件已有的辅助函数和宏，不修改 `trap_dispatch()`、函数签名、Trap frame 布局、`SAVE_ALL` 和 `RESTORE_ALL`。

[SPECIFICATION]

## exception_handler

**Pre-Condition**：`tf` 指向当前有效的 Trap frame，调用者已经确认该 Trap 是异常；对于非法指令和断点异常，`tf->epc` 指向可读取的内核指令。

**Post-Condition**：异常被分类并完成对应处理；对于实验要求处理的异常，`tf->epc` 指向异常指令之后，函数返回后可以进入统一的 Trap 恢复流程。

**Case 1**：原因是 `CAUSE_ILLEGAL_INSTRUCTION` 时，输出非法指令及其地址，读取 `tf->epc` 处的 16 位指令半字；若低两位不为 `11`，将 `tf->epc` 增加 2，否则增加 4。

**Case 2**：原因是 `CAUSE_BREAKPOINT` 时，输出断点及其地址，按照相同的指令长度规则更新 `tf->epc`，避免返回后再次执行断点指令。

**Case 3**：原因不是实验要求处理的异常时，调用 `print_trapframe(tf)` 输出完整 Trap frame，不擅自修改 `tf->epc`，也不伪造异常恢复结果。

**Requirements**：必须根据实际指令编码判断 2 字节或 4 字节长度；不能统一假定所有异常指令长度为 4 字节；不能修改 `struct trapframe`、异常入口和已有中断处理逻辑；保留 `lab3` 标记和学号。
