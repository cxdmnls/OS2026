# Lab2 阶段交付：物理内存初始化

本分支按 `code/` 和 `report/` 提交。常侯鑫已实现 `kern/mm/pmm.c` 的 `page_init()`；First-Fit 使用框架提供的完整实现，Best-Fit 和 Buddy System 由队友继续完成。

## 编译和验证

在 Linux / WSL 中安装并选择 RISC-V 裸机工具链和 **QEMU 4.1.1**，随后在本目录执行：

```bash
make -j4
make qemu
```

看到 `check_alloc_page() succeeded!` 后，内核会停留在无限循环。按 Ctrl+A，松开后按 X 退出 QEMU。

本阶段专用验证会在临时目录构建，检查框架 Page Init 测试、所有页描述符的地址转换和保留状态，以及无效内存范围：

```bash
python3 tools/verify_page_init.py --log-dir ../report/logs
```

脚本使用 `PATH` 中的 `riscv64-unknown-elf-*` 和 `qemu-system-riscv64`，先确认选中了操作系统课程环境。无需修改编译原理课程的环境或全局 PATH。

## 队友接手

- 邓耀飞：完成 `kern/mm/best_fit_pmm.c`，保留原有 `basic_check()`、`best_fit_check()`，实现 `best_fit_pmm_manager`。目前此文件保留原始骨架。
- 张岩：新增 Buddy System 模块和独立测试，并扩展分配器选择方式。不要覆盖已完成的 `page_init()`。
- Best-Fit 完成后，执行 `make clean`，再执行 `make PMM=best_fit qemu` 和 `make PMM=best_fit grade`。`PMM=best_fit` 会选择对应管理器并编译该模块。
- 当前默认 `PMM=first_fit`，用于让初始化部分可以独立编译验证。切换模式前必须执行 `make clean`。最终整体验收前需明确采用 Best-Fit（可将默认值改为 `best_fit`），不能把默认 First-Fit 的验证当成 Best-Fit 验证。
- 本阶段未完成整次 lab2，不能期望当前 `make grade` 完整通过：它还要求 Best-Fit 实现和接入。框架测试及 `YOUR CODE` 标记按各负责人实际实现继续填写。

## 实现边界

启动页表和 `nbase` 固定对应 `DRAM_BASE=0x80000000`。`page_init()` 从设备树读取内存大小，最多管理到 `PHYSICAL_MEMORY_END=0x88000000`，不会管理尚未映射或超出框架上限的物理页。`npage` 是管理上界的绝对物理页号，描述符个数为 `npage - nbase`。

本阶段只排除指导书规定的 OpenSBI、内核镜像及描述符数组；原框架未实现完整的设备树 reserved-memory / 多内存区管理，不能把它当成通用硬件内存探测器。该问题可在后续扩展中完善。
