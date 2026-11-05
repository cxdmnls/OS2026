# Lab3 Prompt 记录

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
