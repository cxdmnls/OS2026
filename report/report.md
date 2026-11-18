# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab4：进程管理 |
| **小组成员** | 2413603-常侯鑫、2411834-张岩、2413404-邓耀飞 |
| **完成日期** | 2026-11-18 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2413603-常侯鑫 | 前置依赖迁入；模块一：page_insert、page_remove；Challenge：深入理解不同分页模式的工作原理（思考题） |
| 2411834-张岩 | 模块二：进程创建与初始化；负责对应报告、Prompt 和 PCB/线程启动原理 |
| 2413404-邓耀飞 | 模块三：调度与上下文切换；Challenge：中断保存与恢复；最终集成验证 |

---

## 一、实验目的

1. 理解 Sv39 页表映射、物理页引用计数和 TLB 一致性。
2. 掌握内核线程控制块、内核栈和首次运行现场的构造。
3. 理解线程选择、页表切换和寄存器上下文切换的分工。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2413603-常侯鑫 | Codex | GPT-6.1 Sol | 依赖迁入与页映射模块 |
| 2411834-张岩 | Codex | DeepSeek V4 Flash | 进程创建与初始化模块 |
| 2413404-邓耀飞 | Codex | GPT-6 | 调度与上下文切换模块、中断保存与恢复思考题、集成验证 |

采用 WSL Ubuntu 24.04、GNU Make 与 RISC-V 交叉编译工具。目标文件检查使用系统 GCC 13.2.0；模块动态验证启用已有课程工具环境，使用 QEMU 4.1.1 和 OpenSBI v0.4。最终集成使用 WSL、GCC 14.2.0、QEMU 4.1.1、OpenSBI v0.4 和 gdb-multiarch。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

初始化物理内存与页表后，构造可运行的内核线程，再通过调度器分配 CPU，保存旧线程并恢复新线程的上下文。

### 3.2 功能的逐步实现

1. 迁入物理内存管理和 Trap 处理基础，保留 Lab4 新增接口。
2. 完成页映射与解除映射，维护引用计数和 TLB。
3. 创建 idleproc 和 initproc，构造 trapframe 与 context。
4. 通过 cpu_idle、schedule、proc_run、switch_to 完成线程运行切换。

---

## 四、实验内容与实现

### 前置依赖：物理内存管理与 Trap

**负责人：** 2413603-常侯鑫

#### 模块功能描述

从已完成的 Lab3 分支迁入其包含的 Lab2 内存管理与 Lab3 Trap 实现。page_init 根据 DTB 内存范围初始化 Page 描述符和空闲内存；分配器提供内核栈及页表所需的连续物理页；Trap 入口保存寄存器，调用 C 处理函数并恢复现场。

Lab4 的 default_pmm.c 原有自检针对 Best-Fit，迁入既有 Best-Fit 实现并以 default_pmm_manager 对外提供接口，保留原自检。保留 Lab4 的 forkrets、get_pte、SSTATUS_SUM 设置和中断保护包装。时钟处理保留事件重新装载和计数，不迁入 Lab3 演示用自动关机阈值，以免终止 Lab4 线程执行。

#### 最终提示词

本阶段实际任务指令的结构化整理见 prompt.md 的“框架准备与前置依赖迁入”。

#### 实现迭代过程

迁入时发现 default_pmm.c 引用了未提供的 best_fit_pmm.h，而实际对外接口为 default_pmm_manager。改用现有 default_pmm.h，并将既有 Best-Fit 管理器导出名称适配为 default_pmm_manager；原自检函数保持不变。Lab4 Page 新增的页面置换链表和虚拟地址字段在 page_init 中一并初始化。

### 功能模块一：虚拟内存页操作

**负责人：** 2413603-常侯鑫

#### 模块功能描述

代码位于 `code/kern/mm/pmm.c`，保留 `//LAB4 function1 2413603` 标记。

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

`page_insert` 先通过 `get_pte(pgdir, la, 1)` 查找或创建末级页表项。成功后先增加目标物理页的引用计数；若原映射就是同一页，则撤销多加的引用，更新权限后保持原引用计数。若原映射指向其他页，则通过内部辅助函数 `page_remove_pte` 撤销旧映射并减少旧页引用，引用归零时释放旧页。最后用 `pte_create(page2ppn(page), perm)` 写入新项，并调用 `tlb_invalidate`。

`page_remove` 使用 `create=0` 查询，保证删除动作不分配页表。页表不存在或目标项无效时直接返回，因此重复删除是安全的。有效映射先清空页表项、刷新对应 TLB，再减少物理页引用；引用归零后释放。它不回收中间页表页，后者由页表生命周期管理或框架自检统一处理。

对于分配失败，`page_insert` 返回 `-E_NO_MEM`，且不改变目标页引用。如果 `get_pte` 刚创建第一级目录页，却在下一次分配失败，则撤销该新目录并释放它，恢复失败前的空闲页数量；原先存在的目录不在回滚范围内。

当前接口沿用框架的 4 KiB 末级页映射约定，不扩展为大页拆分接口。`perm` 保留框架及调用者给出的权限；仅有 `PTE_V` 的测试项可用于软件页表检查，真正访问映射仍需符合 RISC-V 的 R/W/X 权限约束。

#### 最终提示词

提示词见 [prompt.md](prompt.md) 的“页映射操作”。实现后的需求稿依据[提示词工程说明](http://oslab.mobisys.top/lab2026/_book/lab0.5/2_prompt_engineering.html)修订，明确标记为修订稿，不将其视为先前代码生成时逐字使用的原始记录。

提示词从“让内核具备页映射维护能力”描述任务，再列接口和上下文。`[RELY]` 区分虚拟地址、物理页号和 Page 描述符，说明 `get_pte` 的分配副作用及引用计数语义；`[GUARANTEE]` 列出公开接口、允许修改的范围和应保留的框架；`[SPECIFICATION]` 以新建、同页更新、替换、有效删除、无映射删除、分配失败六类场景规定可观察结果，而不规定具体算法步骤。

每类结果都有检查依据：页表项的页号和权限、引用计数、空闲页数量、错误码以及重复操作后的状态。TLB 失效要求通过失效调用审查和映射访问自检核对，不能仅凭 PTE 内容断言缓存已经更新。

#### 实现迭代过程

实现时重点处理两类边界：同页更新不能先释放原物理页，否则可能将仍需映射的页放回空闲链表；多级页表分配失败不能遗留新建的空目录。采用“先持有新引用、再处理旧映射”和“仅撤销本次新建目录”的方式解决。TLB 失效汇编增加 `memory` clobber，约束编译器对页表内存访问的重排。

### 功能模块二：进程创建与初始化

**负责人：** 2411834-张岩

#### 模块功能描述

本模块位于 `code/kern/process/proc.c`，保留 `//LAB4 function2 2411834` 标记，实现 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread` 和 `copy_mm`。它负责把内核从“只能顺序执行初始化代码”推进到“具有线程控制块、内核栈、首次运行现场和可登记的新内核线程”。

`struct proc_struct` 是本实验的进程控制块，也承担内核线程控制块的作用。`state` 和 `need_resched` 描述线程是否可参与调度；`pid`、`parent`、`list_link` 和 `hash_link` 用于身份、父子关系及两类索引；`kstack` 保存独立内核栈的虚拟地址；`context` 保存切换后继续执行的入口和栈；`tf` 保存一次完整 Trap 现场；`mm` 和 `pgdir` 描述地址空间。Lab4 的内核线程共享内核页表，因此 `mm` 为空，`pgdir` 统一设置为 Sv39 页表根的物理地址 `boot_pgdir_pa`。

`alloc_proc()` 通过 `kmalloc()` 创建 PCB。所有会影响后续资源管理的字段都显式初始化：状态为 `PROC_UNINIT`，pid 为 -1，父进程、内存管理结构和 trapframe 为空，切换上下文清零，名称清零，链表节点完成自环初始化。`proc_init()` 初始化进程链表和 pid 哈希桶，再把当前初始化现场包装为 pid 0 的 `idleproc`。该线程直接复用 `bootstack`，将 `need_resched` 置 1，并把 `current` 指向自身。随后 `kernel_thread()` 创建 pid 1 的 `initproc`。

`kernel_thread()` 先构造临时 trapframe。`gpr.s0` 保存目标函数，`gpr.s1` 保存参数，`epc` 指向 `kernel_thread_entry`。`sstatus` 中设置 `SPP`，使 `sret` 返回 Supervisor 模式；设置 `SPIE` 以保留返回后的中断使能模板；清除 `SIE`，确保首次上下文尚未完全建立时不会提前进入中断。临时 trapframe 交给 `do_fork()` 后，`alloc_proc()`、`setup_kstack()`、`copy_mm()` 和 `copy_thread()` 共同完成 PCB、两页内核栈、地址空间和首次运行现场。

`copy_thread()` 把 trapframe 放在新内核栈顶端，把子线程的 `a0` 清零，并让 `gpr.sp` 指向新线程可用的内核栈。`context.ra` 指向 `forkret`，`context.sp` 指向 trapframe。`do_fork()` 在中断保护区内分配 pid、加入 pid 哈希桶和进程链表，最后调用 `wakeup_proc()` 将线程设为 `PROC_RUNNABLE`。本阶段 `copy_mm()` 不复制用户地址空间，只检查当前内核线程没有 `mm`，并让新线程继续共享内核地址空间。

内核线程首次运行的调用链如下：

```text
schedule
  -> proc_run
  -> switch_to(load context.ra/context.sp)
  -> forkret
  -> forkrets(current->tf)
  -> __trapret
  -> sret(sepc=kernel_thread_entry)
  -> kernel_thread_entry
  -> fn(arg)
```

这条链把两类上下文区分得很清楚：`struct context` 只覆盖普通函数之间的线程切换，`struct trapframe` 则覆盖 RISC-V 的寄存器、`sstatus` 和 `sepc` 恢复。`kernel_thread_entry` 把 `s1` 中的参数移入 `a0`，再跳转到 `s0` 保存的函数指针。`forkrets` 把 trapframe 地址放入 `sp`，`__trapret` 恢复全部寄存器和四个 CSR，最后由 `sret` 进入预定入口。

#### 最终提示词

完整结构化 Prompt 见 [prompt.md](prompt.md) 的“张岩：进程创建与初始化”。其中 `[RELY]` 明确 PCB 字段、页表物理地址、内核栈、`context` 与 `trapframe` 的关系，以及 `sstatus.SPP/SPIE/SIE` 的含义；`[GUARANTEE]` 固定六个函数接口和允许修改的范围；`[SPECIFICATION]` 分别规定 PCB 初始化、idleproc、initproc、首次运行现场、并发登记和失败清理的可观察结果。

#### 实现迭代过程

实现中首先检查了 `boot_pgdir_va` 和 `boot_pgdir_pa` 的区别。进程切换最终需要把页表根物理页号写入 `satp`，因此 PCB 不能保存内核虚拟地址。其次检查了 RISC-V 的首次启动栈：`__trapret` 会恢复 trapframe 中保存的 `sp`，所以内核线程不能沿用“参数 stack 为 0”的旧习惯，而应在 `copy_thread()` 中回退到新线程的内核栈。

失败路径按资源取得顺序逆序清理：内核栈分配失败时释放 PCB；地址空间初始化失败时先释放内核栈、再释放 PCB。pid/链表/哈希登记的复合更新放入 `local_intr_save/restore` 临界区，避免同一时刻观察到只完成一半的进程登记状态。

`proc.c` 已通过 RISC-V 交叉编译器对象级构建。完整线程启动还依赖模块三的 `schedule()`、`proc_run()` 和 `switch_to()`；模块二验收与整组最终评分在第五节分别记录，不把对象编译通过描述成 QEMU 全流程通过。

### 功能模块三：调度与上下文切换

**负责人：** 2413404-邓耀飞

#### 模块功能描述

`cpu_idle()` 检查 `need_resched` 并触发调度；`schedule()` 关闭中断并清除调度标记，从当前线程之后循环查找非 idle 的 `PROC_RUNNABLE` 线程，跳过链表头和不可运行线程，没有候选时使用 idle。idle 即使登记在链表中也只作为保底选项，不能抢在真实可运行线程前面。选中后增加 `runs`，仅在目标不同于 current 时调用 `proc_run()`。

`proc_run()` 在中断保护区内更新 `current`，将 `(SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT)` 写入 satp，刷新 TLB，然后调用 `switch_to()`。这里的 pgdir 是物理地址，不能使用 Sv32 的 `lsatp()`。

`switch_to()` 按 `struct context` 布局保存和恢复 `ra`、`sp`、`s0` 至 `s11`，共 14 个 64 位字段。`a0` 和 `a1` 分别指向旧、新 context，最后 `ret` 跳到新 context.ra。由于这是正常函数调用边界，调用者负责临时寄存器和参数寄存器，不需要像 Trap 入口一样保存全部通用寄存器。

新线程的 ra 预设为 forkret，因此第一次切换沿 `forkret -> forkrets -> __trapret -> sret -> kernel_thread_entry -> fn(arg)` 执行；旧线程再次被选中时，恢复自己的栈，继续执行上次 switch_to 后的中断状态恢复。

#### 最终提示词

四段式提示词见 `prompt.md` 的“邓耀飞：调度与上下文切换”，依赖项明确 context 偏移、Sv39 satp 和首次启动链路；规格覆盖正常选择、空闲保底、无需切换及重复恢复。

#### 实现迭代过程

四个接口实现后，全量构建通过，普通启动成功进入 initproc。正式评分前依次处理四项兼容问题：`kmalloc.c` 将传给 `kva2page` 的整数地址显式转为 `void *`；测试线程返回类型由 void 改为与 `kernel_thread` 一致的 int，并补充不可达 return；通过 `.gitattributes` 固定 Shell 脚本为 LF；GDB 缺少交叉版本时回退到 gdb-multiarch，并在评分连接 QEMU 前加载 bin/kernel、指定 riscv:rv64。

最初评分出现空串口输出，调试发现 gdb-multiarch 报 `Truncated register 37 in remote 'g' packet`，内核并未获准继续执行。显式指定架构后，原有三组评分均通过。未改变测试断言、评分分值或调度压力；测试线程仍多次主动让出 CPU。

本实验框架 `do_exit()` 仍调用 panic。initproc 输出结束后进入调试监控器属于该占位行为，不能描述成已实现线程退出回收。

---

### Challenge：深入理解不同分页模式的工作原理（思考题）

**负责人：** 2413603-常侯鑫

#### 不同分页模式与相似代码

| 模式 | 运行架构 | 页表级数 | 每级索引位数 | 页表项大小 | 普通页大小 |
|------|----------|----------|--------------|------------|------------|
| Sv32 | RV32 | 2 | 10 | 4 字节 | 4 KiB |
| Sv39 | RV64 | 3 | 9 | 8 字节 | 4 KiB |
| Sv48 | RV64 | 4 | 9 | 8 字节 | 4 KiB |

Sv39 的有效虚拟地址按 `VPN[2]、VPN[1]、VPN[0]、12 位页内偏移` 分解，高位须按第 38 位符号扩展；Sv48 则多一层索引，高位按第 47 位扩展。多级页表把稀疏地址空间的页表页按需分配，减少未使用地址范围的开销，代价是硬件遍历级数增加，需要 TLB 缓存翻译结果。

本项目实际使用 Sv39：`PDX1` 取位 38～30，`PDX0` 取位 29～21，`PTX` 取位 20～12，每级 512 项。`mmu.h` 顶部存在 Sv32 的旧说明，判断当前实现应以这些宏和 Sv39 的 satp 配置为准。

`get_pte` 的两段相似代码分别处理根页表到中间页表、中间页表到末级页表的导航。每一层都要检查有效位；缺失且允许创建时，分配一页、置引用计数、清零，再把页号写入父级目录项。操作相似源于多级页表逐层索引的结构相同，但两段访问的层级和索引不同。Sv32 对普通页只需一次中间目录处理，Sv39 为两次，Sv48 为三次。通用实现可按层数循环，但还需辨别叶子项和大页，不能直接把所有有效项都当作下一级页表。

#### 页表查询与分配是否应拆分

现有 `get_pte(pgdir, la, create)` 将两者统一，便于小型内核复用遍历代码：`create=0` 只查找，`create=1` 按需分配，调用方代码短且易懂。缺点是布尔参数隐藏了分配、失败和回滚等副作用；查询调用若误传 1，会意外占用物理页，分配失败还可能留下中间结构。

在较大内核中，建议对外拆成纯查询和确保页表存在两个明确接口，内部共享逐层遍历代码。纯查询不分配，创建接口集中管理权限、锁、失败回滚及 TLB 维护。没有必要为拆分复制两套遍历代码，也不必为了本思考题修改框架公开接口。本实现保留 `get_pte` 接口，在 `page_insert` 中补足当前创建路径的失败回滚。

### Challenge：中断保存与恢复

**负责人：** 2413404-邓耀飞

`local_intr_save(intr_flag)` 调用 `__intr_save()`：读取 `sstatus.SIE`，若为 1 则关闭中断并返回 1，否则返回 0。结束时 `local_intr_restore(intr_flag)` 仅在保存值为 1 时重新打开中断。因此进入前关闭的中断不会被错误打开。

嵌套时外层保存 1 并关中断，内层看到关闭状态而保存 0；内层退出保持关闭，外层退出才重新打开。每层必须保存到独立局部变量，不能共用同一个 flag。本实现的 schedule 与 proc_run 正是这种嵌套关系；切回旧线程后才恢复旧栈上保存的状态。

`do { ... } while (0)` 将宏封装成单条语句，只执行一次，保证用于 if/else 时语法完整。中断屏蔽只阻止本核可屏蔽中断造成的并发，不屏蔽同步异常，也不能阻止其他 CPU 修改共享数据；多核仍需锁和内存序机制。切换到新线程后的中断状态由其 Trap frame 和 sret 恢复，不能依赖尚未执行的旧线程恢复语句。

---

## 五、测试与验证

前置依赖目标文件编译命令（在 code 目录执行）：

```bash
make -j2 obj/kern/mm/pmm.o obj/kern/mm/default_pmm.o obj/kern/trap/trap.o obj/kern/trap/trapentry.o
```

2026-11-11 执行结果：四个目标文件均编译成功；出现 WSL 文件时间偏差警告。此检查仅证明迁入文件可编译，不代表完整内核链接或运行通过。

模块一已实现并通过 `make -B obj/kern/mm/pmm.o` 强制编译。完整评分仍需三个模块在同一份源码中集成，不能将单个模块检查等同于整组总分通过。

2026-11-11 在临时源码副本中隔离尚未完成的线程入口，以框架原有 `test_lab4_pmm_basic()` 验证模块一；仓库的启动流程、评分工具和测试文件保持不变。使用 QEMU 4.1.1、OpenSBI v0.4，通过 `-kernel bin/ucore.img` 启动临时镜像。结果如下：

| 模块检查 | 实际结果 |
|----------|----------|
| 临时验证内核编译、链接 | 通过 |
| `check_alloc_page()` | 通过 |
| `check_pgdir()` | 通过 |
| `check_boot_pgdir()` | 通过 |
| `kmalloc_init()`、虚拟内存区域自检 | 通过 |
| 框架 `[Test PMM] PASS` | 通过 |
| 无空闲页时插入返回 `-E_NO_MEM` | 通过 |
| 仅剩一页、中间目录分配后失败的回滚 | 通过，目录项清零、目标引用不变、空闲页数量恢复 |
| 全组完整评分 | 后续模块三集成后已执行，30/30 |

分配失败检查仅在临时副本中运行，没有向仓库加入自编测试脚本。该阶段记录的是模块一的中间进度；最终整组评分在模块三接入后完成，结果见最终集成部分。

模块二于 2026-11-18 先执行：

```bash
make -B obj/kern/process/proc.o
```

`proc.c` 在 `riscv64-unknown-elf-gcc 14.2.0` 下无警告通过编译。随后在独立临时副本中接入只用于验证的最小调度与上下文切换实现，并运行课程 `tools/inject.py` 注入原有测试。临时实现未写入 `lab4_new`，正式模块三仍由 2413404-邓耀飞完成。QEMU 4.1.1、OpenSBI v0.4 的实际输出如下：

```text
[Test PMM] PASS
check_vma_struct() succeeded!
check_vmm() succeeded.
[Test Process Create] PASS
[Test Schedule] PASS
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)".
```

| 模块二检查 | 实际结果 |
|------------|----------|
| `proc.c` RISC-V 交叉编译 | 通过 |
| PCB 基础字段与 `pgdir` 初始化 | 通过 |
| `idleproc` 的 pid、状态、内核栈和父进程字段 | 通过 |
| `initproc` 的 pid、父进程、context 与 trapframe | 通过 |
| pid 分配、哈希桶和进程链表登记 | 通过 |
| `initproc` 经首次运行链输出三行文本 | 通过 |
| 两个附加内核线程均可运行和切换 | 通过 |
| 整组 `make grade` | 2026-11-18 正式集成通过，30/30 |

临时集成发现当前 WSL 的 GCC 14 将框架测试线程的 `void (*)(void *)` 与 `kernel_thread` 的 `int (*)(void *)` 参数视为错误；仅为适配测试调用约定，将注入测试线程的返回类型改为 `int` 并补充不可达返回，测试断言和评分标准不变。同一工具链还拒绝 `kern/mm/kmalloc.c` 中缺少 `void *` 强制转换的旧写法，已补充显式转换。正式评分还要求 GDB 先载入内核并指定 `riscv:rv64`，已在评分生成的临时 GDB 脚本中补齐。

---

### 最终集成与课堂演示

2026-11-18 在当前正式代码执行以下命令：

```bash
cd /mnt/d/Desktop/CODE/os/OS2026/code
export PATH=/opt/qemu-4.1.1/bin:$PATH
qemu-system-riscv64 --version
make clean
make -j2
make qemu
make grade
```

| 项目 | 实际结果 |
|---|---|
| 全量构建 | 通过 |
| 普通启动 | 页表、物理内存、VMM 自检通过，initproc 输出 Hello world |
| PMM | 10/10 |
| Process Create | 10/10 |
| Schedule | 10/10，两个测试线程多次主动调度后返回验证点 |
| 总分 | **30/30** |

评分自动在 `/tmp/lab4_grade` 的源码副本注入测试，GDB 在进入监控器的 readline 处停止，随后检查串口结果。评分不会把测试插入正式源码。普通启动结束出现 `process exit!!` 是框架 do_exit 占位，不表示正常退出已实现。评分通过验证共享内核页表下的线程切换，没有证明独立用户地址空间、多核调度或完整退出回收。

演示时先确认 QEMU 4.1.1；需要展示完整流程时执行构建、普通启动与评分，只检查结果时执行 make grade。讲解时沿 cpu_idle、schedule、proc_run、switch_to 和首次启动链路定位代码，说明 context 与 trapframe 的区别及嵌套中断保护。

---

## 六、实验总结与收获

### 对操作系统的理解

页表是虚拟地址映射的数据结构，TLB 是硬件地址翻译缓存；修改页表后需要使旧缓存失效。PCB 管理线程状态和资源，context 只保存切换执行流所需的寄存器。本实验以内核线程为主，尚未覆盖完整用户程序加载、文件系统和多核调度。

模块二涉及的核心知识点及其与操作系统原理的对应关系如下：

| 本实验机制 | OS 原理知识点 | 关系与差异 |
|------------|---------------|------------|
| `proc_struct` | PCB/TCB | PCB 统一管理身份、状态、栈、地址空间和链接信息。本实验的独立内存资源较少，因此同一结构也直接充当内核线程控制块。 |
| `proc_list` 与 `hash_list` | 进程索引与生命周期管理 | 线性链表用于遍历和调度，pid 哈希桶用于平均常数时间查找；本实验尚未实现完整退出、等待和回收。 |
| 两页内核栈 | 特权级切换与内核栈 | 每个线程保存自己的内核栈，Trap 和内核函数调用使用该栈；此时用户栈和内核栈切换尚未完整实现。 |
| `struct context` | CPU 上下文切换 | 只保存被调用者保存寄存器，调用者保存寄存器由编译器在普通函数调用边界处理，因此比完整进程上下文轻量。 |
| `struct trapframe` | 硬件现场与异常返回 | 保存全部通用寄存器、`sstatus`、`sepc` 等，可供 `sret` 恢复；它不包含页表、文件等软件资源。 |
| `forkret -> forkrets -> __trapret` | 新执行流的首次启动 | 通过“切换上下文到恢复 Trap 现场”的通用路径启动新线程，无需为 `kernel_thread_entry` 另写一套进入逻辑。 |
| `copy_mm()` 占位 | 地址空间复制/隔离 | 真正进程还需要复制或共享页表、维护 VMA 和引用计数；本实验的内核线程共享内核页表，故只保留接口。 |

本实验尚未覆盖但 OS 原理中重要的内容包括：用户态进程的完整创建与 `fork` 语义、写时复制、独立页表切换的完整验证、进程退出后的僵尸态回收和父进程等待、文件描述符继承、信号机制、多核调度与锁，以及睡眠/唤醒和公平调度策略。

### AI 协作开发的经验

模块二的调试重点是上下文链而不是单个函数：PCB 字段正确不代表线程一定能启动，还需要核对 `boot_pgdir_pa`、新线程 trapframe 的落点、`context.ra`、`context.sp`、`sstatus` 和 `sp` 恢复顺序。临时集成中先通过原有 `Process Create` 测试确认对象状态，再利用附加线程确认首次运行链，从而把“能分配 PCB”和“能真正执行内核线程”分开验证。

跨实验迁入需明确数据结构变化和框架保留范围，并使用编译结果核对接口兼容性，不能将旧实验文件直接整体覆盖。
