# Lab4 Prompt 记录

## 常侯鑫：框架准备与前置依赖迁入

以下为实际任务指令的结构化整理。

[PROMPT]

检查仓库状态，从更新后的 main 准备独立 lab4 分支；将提供的 Lab4 框架整理到 code/，按实验报告模板准备 report/report.md、report/prompt.md 和 report/images/；补入 Lab2、Lab3 前置实现。可以使用对应的 lab2、lab3 分支。

[RELY]

- 已提交的 Lab2/Lab3 分支提供物理页初始化、分配器、Trap 处理和汇编恢复实现。
- Lab4 已提供 get_pte、forkrets、Sv39 页表结构、进程框架和原有评分工具。
- default_pmm.c 原有自检为 Best-Fit，需匹配分配策略并保留自检。

[GUARANTEE]

保留原作者学号；按函数迁入并适配 Lab4 接口，不覆盖新增框架。只准备框架与依赖，暂不实现三个 Lab4 模块，不提交或推送。

[SPECIFICATION]

Pre-Condition：检查工作区及远程分支后进行操作，保存本地旧实验遗留产物。
Post-Condition：仓库当前分支为 lab4；代码与报告分目录；前置实现可单独编译。
Case 1：迁入 page_init，使用 Lab4 的内存宏和 Page 布局。
Case 2：迁入与现有自检匹配的 Best-Fit 实现，导出 default_pmm_manager。
Case 3：迁入中断与异常处理，保留定时器重新装载，不迁入 Lab3 自动关机限制。
Case 4：补全 RESTORE_ALL 和 __alltraps 调用，保留 forkrets。
Requirements：不修改原评分工具或自检，不伪造测试结果，不引用尚不存在的截图。

## 常侯鑫：页映射操作

[PROMPT]

为 Lab4 内核提供普通物理页的映射维护能力：调用者能够将已分配的物理页映射到指定虚拟地址，更新该地址的目标页或访问权限，并安全撤销映射。所有操作必须保持页表项、物理页引用计数及 TLB 翻译结果一致，避免释放仍被使用的物理页或在失败时泄漏资源。

功能位于 `code/kern/mm/pmm.c`，对外通过 `page_insert()` 与 `page_remove()` 提供。任务限于现有 Sv39 框架下的 4 KiB 普通页映射，不扩展大页拆分、用户地址空间复制、进程创建或调度功能。

[RELY]

- `pgdir` 是页表根的内核虚拟地址；`la` 是目标虚拟地址，映射粒度为其所在的 4 KiB 页，页内偏移不改变映射目标页。
- `struct Page *page` 是已分配物理页的描述符，不是物理地址。`page2ppn()` 提供物理页号，`pte_create()` 编码页号与权限，`pte2page()` / `pde2page()` 将有效项转换为对应页描述符。
- 现有 `get_pte(pgdir, la, create)` 查询末级项；`create=0` 不分配，`create=1` 可能分配中间页表页，失败返回 `NULL`，且可能已建立部分中间结构。
- 可使用 `page_ref_inc()`、`page_ref_dec()`、`free_page()` 和 `tlb_invalidate()`。一次有效映射持有一个物理页引用，引用归零后才能释放该物理页。中间页表页具有独立的生命周期。
- 权限使用现有 `PTE_V`、`PTE_R`、`PTE_W`、`PTE_X`、`PTE_U` 等宏。保留框架对软件检查项的约定；用于实际访存的权限须符合 RISC-V 约束。
- 框架提供 `check_pgdir()`、`check_boot_pgdir()` 和 `tools/test_lab4.c` 中的 PMM 检查，可作为正常映射路径的验证依据。

[GUARANTEE]

提供以下公开接口，保持签名不变：

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

允许添加内部辅助函数及必要的 TLB 一致性修正；保持 `get_pte()` 公开接口、页表布局、现有自检和评分标准不变。保留 `LAB4` 标记，将本模块 `YOUR CODE` 替换为 `2413603`。不修改其他成员的进程创建、调度与上下文切换模块。

[SPECIFICATION]

**Pre-Condition**：`pgdir` 指向有效页表；插入时 `page` 指向合法、已分配的物理页描述符，原有映射与引用计数一致。目标路径符合框架的普通页约定，调用环境不并发修改同一路径。

**Post-Condition**：插入成功返回 `0`，目标项有效、物理页号及权限符合参数；删除成功后目标映射不存在。所有成功变更对应的旧 TLB 翻译均失效，引用计数和空闲页状态与最终映射一致。

**Case 1：建立新映射**。目标地址原先未映射时，成功后映射到目标物理页，该页引用计数恰好增加 1，其他映射不受影响。

**Case 2：同页更新权限**。目标地址原先已映射同一物理页时，成功后权限反映新的 `perm`，该页引用计数和空闲页数量不因重复映射发生变化；该页不得被释放。

**Case 3：替换目标页**。目标地址原先映射另一物理页时，成功后旧映射不再存在，新页引用增加 1，旧页引用减少 1；旧页仍有其他引用时保留，归零时释放。

**Case 4：删除有效映射**。删除后该项无效，原物理页引用减少 1，仅在归零时释放；其他映射及中间页表页保留。

**Case 5：删除未映射地址**。页表路径缺失或末级项无效时直接返回，不分配页表、不改变引用计数和空闲页数量。连续删除同一地址与删除一次的最终状态一致。

**Case 6：内存不足**。无法建立所需页表时返回 `-E_NO_MEM`，已有映射、目标页引用计数及调用前的空闲页数量保持不变，不遗留本次失败产生的中间页表或有效末级映射。

**Requirements**：验证须对应上述结果，包括同页更新、共享页替换、重复删除和中间页表分配失败。区分编译检查、模块运行和完整评分，仅报告实际结果。实现顺序和内部辅助结构由实现者选择，不用伪代码代替功能需求。

## 张岩：进程创建与初始化

以下为模块二实际使用并整理后的结构化 Prompt。

[PROMPT]

在现有 Lab4/RISC-V uCore 框架中实现模块二“进程创建与初始化”。只修改 `code/kern/process/proc.c` 的模块二位置，完成 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread` 和 `copy_mm`，并保留 `//LAB4 function2 2411834` 标记。完成后内核应能创建 `idleproc` 与 `initproc`，新线程能够沿 `switch_to -> forkret -> forkrets -> __trapret -> kernel_thread_entry -> fn(arg)` 的首次运行链路进入目标函数。不得修改调度、上下文切换、页映射、评分工具和测试文件。

[RELY]

- `struct proc_struct` 是进程/线程控制块。`state`、`pid`、`parent`、`kstack`、`mm`、`context`、`tf`、`pgdir`、`name`、`list_link` 和 `hash_link` 共同描述线程状态、资源和调度信息；刚分配但尚未登记的 PCB 必须完成逐字段初始化。
- `idleproc` 不是由普通 `fork` 复制出的线程，而是把调用 `proc_init` 时的执行现场包装成 pid 为 0 的内核线程。它使用启动阶段的 `bootstack`，并由 `need_resched = 1` 触发第一次调度。
- 内核线程共享内核地址空间。`boot_pgdir_pa` 是 SV39 页表根的物理地址，可直接交给 `satp`；不能把 `boot_pgdir_va` 当作页表物理基址。
- `alloc_pages(KSTACKPAGE)` 返回连续物理页，`page2kva()` 得到其内核虚拟地址。新线程的 trapframe 位于 `kstack + KSTACKSIZE` 下方，`context.sp` 指向该 trapframe。
- `struct context` 只保存切换所需的 `ra`、`sp` 和 `s0` 至 `s11`。新线程的 `context.ra` 设置为 `forkret`；切换后由 `forkret` 恢复 `current->tf`。
- `forkrets` 把 `a0` 中的 trapframe 地址装入 `sp`，再跳转到 `__trapret`。恢复寄存器后执行 `sret`，使用 trapframe 中的 `sepc` 与 `sstatus` 进入首次运行入口。
- `kernel_thread_entry` 把 `s1` 中的参数移入 `a0`，再通过 `s0` 跳转目标函数。因此模板 trapframe 必须把函数指针放在 `gpr.s0`、参数放在 `gpr.s1`，把 `epc` 设为 `kernel_thread_entry`。
- `sstatus.SPP` 决定 `sret` 返回的特权级，内核线程应返回 Supervisor 模式；`SSTATUS_SPIE` 保存进入时允许中断的状态，模板中应清除 `SSTATUS_SIE`，避免在上下文尚未完全建立时被中断。
- `get_pid()` 依赖 `proc_list` 分配唯一 pid；`hash_proc()` 把已分配 pid 的 PCB 加入哈希桶；`wakeup_proc()` 把非僵尸、非就绪线程改为 `PROC_RUNNABLE`。
- `do_exit` 仍为框架占位实现，本次不扩展退出回收逻辑。

[GUARANTEE]

提供并保持以下接口：

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

允许增加内部失败清理标签，但不得改变现有头文件接口、进程状态枚举、`proc_struct` 字段或其他成员已经完成的功能。

[SPECIFICATION]

**Pre-Condition**：`kmalloc`、物理页分配器和 `boot_pgdir_pa` 已初始化；调用 `proc_init()` 时当前执行现场可作为一个内核线程；模块三最终提供 `schedule()`、`proc_run()` 和 `switch_to()`。

**Post-Condition**：`proc_list` 与 1024 个 pid 哈希桶已初始化；`idleproc` 的 pid 为 0、状态为 `PROC_RUNNABLE`、内核栈为 `bootstack`、父进程和 `mm` 为空；`initproc` 的 pid 为 1、父进程为 `idleproc`、状态为 `PROC_RUNNABLE`、具有独立内核栈和经过初始化的首次运行现场。

**Case 1：分配 PCB**。`alloc_proc()` 必须返回安全初始化的 PCB，或内存不足时返回 `NULL`。初始状态为 `PROC_UNINIT`，pid 为 -1，`runs`、标志和名称等字段为 0/NULL，`pgdir` 为内核页表物理地址。

**Case 2：初始化 idleproc**。`proc_init()` 先建立全局结构，再分配并登记 `idleproc`；`current` 指向 `idleproc`。该线程不需要新建内核栈。

**Case 3：创建 initproc**。`kernel_thread(init_main, "Hello world!!", 0)` 返回正 pid。新线程拥有 `KSTACKPAGE` 个连续页的内核栈，其 trapframe 位于栈顶，`context.ra=forkret`，`context.sp` 指向 trapframe。

**Case 4：构造首次运行现场**。模板 trapframe 清零后设置 `s0` 为目标函数、`s1` 为参数、`epc` 为 `kernel_thread_entry`；`sstatus` 显式进入 Supervisor 模式并保留中断使能模板，同时暂时关闭 S 模式中断。

**Case 5：登记新线程**。pid 分配、哈希表插入、进程链表插入和 `nr_process` 增加应处于同一中断保护区间，之后 `wakeup_proc()` 新线程。成功后 `do_fork()` 返回子线程 pid。

**Case 6：失败清理**。PCB 分配失败时不泄漏资源；内核栈分配失败时释放 PCB；后续初始化失败时先释放内核栈，再释放 PCB。进程总数达到 `MAX_PROCESS` 时返回 `-E_NO_FREE_PROC`。

**Case 7：本阶段地址空间语义**。`copy_mm()` 只服务于内核线程，要求 `current->mm == NULL`，并使新线程的 `mm` 为空。用户地址空间复制留给后续实验。

**Requirements**：至少验证进程对象创建、`idleproc/initproc` 字段、pid、链表/哈希登记、内核栈与 trapframe 地址、模块二目标文件编译，以及模块三接入后的首次线程启动。不得把模块二编译通过等同于整组 `make grade` 已通过。

