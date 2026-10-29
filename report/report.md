# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab3：断，都可以断（Trap、中断与异常处理） |
| **小组成员** | 2413603-常侯鑫、2411834-张岩、2413404-邓耀飞 |
| **完成日期** | 2026-10-28 |

### 小组分工

代码分工：

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2411834-张岩 | 第一部分：`RESTORE_ALL` 与 `__alltraps`，Trap 上下文恢复和入口调用 |
| 2413603-常侯鑫 | 第二部分：`interrupt_handler()`，中断分类与时钟中断 |
| 2413404-邓耀飞 | 第三部分：`exception_handler()`，异常处理、代码集成与课堂演示 |

报告分工：

- 张岩：整体逻辑线、第一部分实现、对应 Spec/Prompt、Bug 记录及 OS 原理对照。
- 常侯鑫：中断与时钟模块的实现、Prompt 和测试记录，待补充。
- 邓耀飞：异常处理、集成测试和课堂演示部分，待补充并统一整合。

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
| 2413404-邓耀飞 | 待补充 | 待补充 | 负责异常、集成与演示 |

运行及编译环境：

- WSL 下的 GNU/Linux 环境
- `riscv64-unknown-elf-gcc 14.2.0`
- GNU Assembler 2.45.50
- QEMU 4.1.1
- 构建命令：`make`
- 运行命令：`make qemu`
- 评分命令：`make grade`

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
- 当前合并后的完整 `make` 仍因另外两个处理函数尚未实现而停止：

  ```text
  implicit declaration of function 'interrupt_handler'
  implicit declaration of function 'exception_handler'
  ```

  这不是第一部分本身的汇编错误，而是功能模块二尚未接入的预期集成依赖；完整 QEMU 与 `make grade` 验证需在其他成员完成代码后执行。

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

待负责人完成代码后，在本节补充实际实现过程、最终 Prompt、迭代中遇到的 Bug、100 ticks/200 ticks 以及未知中断测试结果。

---

### 功能模块三：异常处理

**负责人：** 2413404-邓耀飞

**代码位置：** `code/kern/trap/trap.c`

**核心函数：**

```c
void exception_handler(struct trapframe *tf);
```

该模块负责按照 `scause` 识别并处理断点异常和非法指令异常。处理时需要输出规定信息，并正确更新 `tf->epc`，使 `sret` 返回后不会再次执行触发异常的指令。对于本实验未单独处理的异常，应调用 `print_trapframe(tf)` 进行兜底。

待负责人完成代码后，在本节补充实际实现过程、最终 Prompt、迭代中遇到的 Bug、断点返回、非法指令返回和未知异常兜底测试结果。

---

## 五、测试与验证

当前已完成第一部分的汇编级验证：

| 检查项 | 结果 |
|--------|------|
| `trapentry.S` 单独汇编 | 通过 |
| `__alltraps` 符号存在 | 通过 |
| `__trapret` 符号存在 | 通过 |
| 恢复顺序检查 | 通过，`sp` 最后恢复 |
| 完整性 `make` | 待常侯鑫、邓耀飞补充处理函数 |
| QEMU 运行 | 待集成后执行 |
| `make grade` | 待集成后执行 |

汇编验证结果：

```text
                 U trap
0000000000000002 T __alltraps
000000000000006a T __trapret
```

完整测试截图和 `make grade` 结果待代码集成后补充到 `report/images/` 与 `report/logs/`。

---

### 中断与时钟测试

待常侯鑫完成 `interrupt_handler()` 后补充：

- `IRQ_S_TIMER` 是否能持续产生并重新装载下一次时钟事件。
- `ticks` 是否逐次累加，并在 100 ticks 时输出统计信息。
- 200 ticks 后内核是否仍能正常返回并继续运行。
- 软件中断和未知中断是否进入正确的输出分支。

---

### 异常处理测试

待邓耀飞完成 `exception_handler()` 后补充：

- 执行断点指令后，`sepc` 是否推进并返回断点后的下一条指令。
- 执行非法指令后，是否输出异常信息并正确跳过异常指令。
- 指令长度不同时，`tf->epc` 的推进量是否正确。
- 未知异常是否调用 `print_trapframe(tf)`，且不会形成异常循环。

---

### 最终集成验证

完成 `interrupt_handler()` 和 `exception_handler()` 后，执行：

```text
make
make qemu
make grade
```

最终需要记录 Trap 上下文与断点返回、非法指令异常返回、持续时钟中断三组测试结果及总分。同时检查所有成员模块合并后仍保留对应的 `lab3` 注释和各自学号。

提交前还需完成：

- [ ] 常侯鑫补充中断与时钟模块及测试。
- [ ] 邓耀飞补充异常处理模块及测试。
- [ ] 后续 Prompt 完成后再补入 `report/prompt.md` 和对应模块。
- [ ] 测试截图放入 `report/images/`。
- [ ] 测试日志放入 `report/logs/`。
- [ ] 删除当前仅针对未集成状态的待办说明。
- [ ] 确认报告中的测试结论全部来自实际运行结果。

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

AI 生成的代码仍需人工检查实际反汇编和构建结果。发现 `sbadaddr` 兼容问题后，不能直接忽略编译错误，也不能擅自改变 trapframe 布局，而应确认 CSR 编号和语义后再做兼容处理。当前报告只记录已完成的汇编验证；最终测试结论需在另外两个功能模块集成后补充。

## 参考资料

- [Lab3 实验指导书](http://oslab.mobisys.top/lab2026/_book/lab3/lab3.html)
- [Lab3 练习说明](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_2_1_exercise.html)
- [Lab3 报告要求](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_4_lab_requirement.html)
- [Lab2 报告格式参考](https://github.com/cxdmnls/OS2026/blob/lab2/report/report.md)

---
