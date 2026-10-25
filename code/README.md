# Lab2 阶段交付：物理内存初始化

本分支按 `code/` 和 `report/` 提交。常侯鑫实现 `page_init()`，张岩实现 Buddy System，邓耀飞完成 First-Fit 分析和 Best-Fit 实现。默认分配器为 Best-Fit。

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

- Best-Fit 已实现，原有 `basic_check()`、`best_fit_check()` 保持不变，并补充确定性行为测试。
- Buddy 与 `page_init()` 保留团队原实现。
- `make clean` 后可运行 `make PMM=best_fit qemu`、`make PMM=best_fit grade`。
- 默认 `PMM=best_fit`，可用 `PMM=first_fit` 或 `PMM=buddy` 切换；切换前必须 `make clean`。
- 2026-10-25 已运行 Best-Fit 评分，结果 20/20；详细分析见 `../report/report.md`。
- Makefile 优先选择 `/opt/qemu-4.1.1/bin/qemu-system-riscv64`，亦支持 `QEMU=...` 覆盖。系统新版本未被覆盖。
- 提示词草稿暂存仓库外，`report/prompt.md` 未修改。

## 实现边界

启动页表和 `nbase` 固定对应 `DRAM_BASE=0x80000000`。`page_init()` 从设备树读取内存大小，最多管理到 `PHYSICAL_MEMORY_END=0x88000000`，不会管理尚未映射或超出框架上限的物理页。`npage` 是管理上界的绝对物理页号，描述符个数为 `npage - nbase`。

本阶段只排除指导书规定的 OpenSBI、内核镜像及描述符数组；原框架未实现完整的设备树 reserved-memory / 多内存区管理，不能把它当成通用硬件内存探测器。该问题可在后续扩展中完善。
