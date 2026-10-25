# Lab2：物理内存、对象分配与 MMU 调试

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
- Makefile 使用当前环境的 `QEMU` 或 PATH 中的 `qemu-system-riscv64`，支持 `QEMU=...` 覆盖。
- 本轮补充规格、复核及新任务提示词见 `../report/prompt.md`；未把新整理的内容伪装成队友历史会话。

## 本机环境与扩展验证

WSL 中激活 OS 课程隔离环境：

```bash
source /mnt/e/Develop/Pycharm/PycharmData/OS/environment/oslab-env.sh
cd /mnt/e/Develop/Pycharm/PycharmData/OS/OS2026/code
make check-extensions
make trace-mmu
```

本机为 QEMU 4.1.1、SiFive GCC 10.2.0、Make 4.3。`check-extensions` 在三种 PMM
上验证 SLUB 对象分配和两次真实 ld 读取，最后保留 Best-Fit 构建。
`trace-mmu` 需要原生 GDB（本机 15.1）与工具链的 RISC-V GDB，以及带符号的
QEMU；默认复用 `/home/lenovo/oslab/build/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64`，
可通过 `MMU_QEMU` 改路径。它动态读取符号、选择端口，记录真实 CPU 查表和
软件 TLB 回填，不要求运行者手工协调两个 GDB 的等待关系。

新增源码：`slub.c/slub.h`、独立的 `slub_test.c`、`mmu_probe.c/mmu_probe.h`。
`kern_init()` 在 PMM 检查后执行对象分配测试和 MMU 探针，最后保持无限循环。
所有实际输出在 `report/logs/`；截图按小组安排后续采集到 `report/images/`。

当前以教学用单执行流实现大小类、位图对象槽和空 slab 页回收，不等于 Linux
完整 SLUB；页表调试观察到的是顶层大页叶子，报告没有声称实际查遍三层。

## 实现边界

启动页表和 `nbase` 固定对应 `DRAM_BASE=0x80000000`。`page_init()` 从设备树读取内存大小，最多管理到 `PHYSICAL_MEMORY_END=0x88000000`，不会管理尚未映射或超出框架上限的物理页。`npage` 是管理上界的绝对物理页号，描述符个数为 `npage - nbase`。

本阶段只排除指导书规定的 OpenSBI、内核镜像及描述符数组；原框架未实现完整的设备树 reserved-memory / 多内存区管理，不能把它当成通用硬件内存探测器。该问题可在后续扩展中完善。
