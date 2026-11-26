# Lab5 Prompt 记录

## 常侯鑫：前置代码迁入

以下是实际迁入任务的结构化整理。

[PROMPT]

将提供的 Lab5 框架整理到独立 lab5 分支，参考 GitHub 对应 Lab2、Lab3、Lab4 分支补齐前置代码，使物理内存、页映射、内核线程调度及 Trap 基础可供后续用户进程模块使用。当前阶段仅迁入和适配基础，不实现新的系统调用、用户程序加载、用户地址空间复制或 COW。

[RELY]

- Lab2 已完成 First-Fit，Lab4 已整合物理内存初始化、普通页映射和内核线程切换。
- Lab5 的分配器自检对应 First-Fit，default_pmm.h 将 free_area 声明为外部变量。
- Lab5 提供进程树 set_links/remove_links、新增 PCB 字段及支持用户栈切换的 SAVE_ALL。
- 返回用户态时 sscratch 应保存可用的内核栈顶；在内核态运行时 sscratch 为零。

[GUARANTEE]

按函数迁入，保留原作者实验标记和学号，保留 Lab5 新接口、自检、用户程序及评分文件。新增实验功能的 YOUR CODE 标记暂时保留，后续由对应负责人完成。

[SPECIFICATION]

Pre-Condition：工作区干净，读取远程对应分支，保护已完成的实验成果。
Post-Condition：独立 lab5 分支采用 code/ 与 report/ 布局，基础目标文件能够编译。
Case 1：First-Fit 实现匹配 Lab5 自检及 free_area 导出要求。
Case 2：迁入 page_init、page_insert、page_remove，保留 Lab5 已提供的批量解除映射和页表回收接口。
Case 3：迁入内核线程基础，新增 PCB 字段初始化，登记使用 Lab5 进程树接口，保留唤醒时清除 wait_state 的逻辑。
Case 4：Trap 恢复按 SPP 设置 sscratch，最后恢复 sp，保留 Lab5 的 SAVE_ALL、forkrets 和用户态返回检查。
Requirements：不以目标编译成功代替完整运行或评分；不提交或推送。

## 常侯鑫：系统调用与进程生命周期

以下记录本阶段实际实现要求与边界。

[PROMPT]

在 Lab5 现有 Trap、PCB、进程树及用户内存检查接口上实现系统调用入口与分发，并完成 `do_exit()`、`do_wait()`、`do_yield()`、`do_kill()`。按 RISC-V ABI 从 `a0` 读取系统调用号、从 `a1` 至 `a5` 读取参数，结果放回保存现场的 `a0`。用户 `ecall` 返回后跳过 4 字节指令。覆盖 Lab5 已定义的系统调用号，非法或未实现的编号返回 `-E_INVAL`。

退出时拒绝 idle/init 进程退出；切换至内核页表后释放最后引用的用户地址空间，保存退出码并将进程置为 zombie，唤醒等待其退出的父进程，将子进程转交 init，保留当前内核栈和 PCB 供父进程回收。等待支持指定 PID 与 PID 0（任意子进程），验证可选退出码指针；发现 zombie 时复制退出码、解除哈希和父子链表关系并释放其内核栈及 PCB；仍有目标子进程时登记 `WT_CHILD` 并休眠，不存在目标子进程时返回错误。yield 请求重新调度。kill 验证目标 PID，拒绝杀死 idle/init、zombie 或已在退出的进程，并设置退出标记、唤醒可中断的睡眠目标。

[RELY]

- `trap()` 进入时将当前 Trap frame 暂存于 `current->tf`；系统调用入口使用项目现有 `syscall()` 接口。
- `proc_struct` 已包含 `exit_code`、`wait_state`、父子关系链表及 `PF_EXITING`；调度器和 `wakeup_proc()` 使用已有进程状态约定。
- `user_mem_check()`、`copy_from_user()`、`copy_to_user()` 用于检查并访问用户空间；退出地址空间前不得继续依赖旧用户指针。
- 当前分工中用户程序加载和地址空间复制由队友实现；不改写其 `do_execve()`、`copy_mm()` 等模块。

[GUARANTEE]

保持 Lab5 系统调用号、函数接口和既有成员标记；只实现本阶段负责的 Trap 系统调用入口/分发及进程退出、等待、让出和终止请求。资源回收顺序必须保证退出进程不在当前页表仍被使用时释放该地址空间，且不释放仍在运行栈上的 PCB/内核栈。

[SPECIFICATION]

Pre-Condition：Lab5 前置迁入已完成，工作分支为 `lab5`，后续用户程序加载与进程复制模块尚未集成。
Post-Condition：三个负责对象文件可编译；阶段测试中的进程创建、系统调用入口/非法编号和进程控制检查通过；不声称整套 Lab5 完成。
Case 1：用户 `ecall` 前进 `epc` 并按系统调用号分发，返回值写回 `a0`。
Case 2：wait 发现僵尸子进程时回收资源；没有子进程时返回错误；有未退出子进程时休眠并在子进程退出后继续检查。
Case 3：kill 对无效 PID 或禁止终止的进程返回错误；对有效目标设置退出标记，并唤醒可中断睡眠目标。
Case 4：yield 设置重调度请求；用户异常退出通过进程退出路径清理。
Requirements：不修改队友负责的 ELF 加载、地址空间复制和 COW 实现；不加入未实际运行的测试或结果；临时验证桩与脚本不得提交；不执行 git commit 或 push。
