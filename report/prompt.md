# Lab2 提示词记录

## 1. 三人分工分析（原始用户提示词）

> 我们组有三个人你觉得应该如何分工呢？

结合前文实验要求，讨论得到的分工为：常侯鑫负责物理内存探测、`page_init()` 与基础集成；邓耀飞负责 First-Fit 分析与 Best-Fit；张岩负责 Buddy System 及其测试。三人共同复核接口、运行测试并准备答辩。

## 2. Buddy System 实现与测试（张岩）

## 原始任务

根据小组分工，本部分只负责 Buddy System 分配算法及其测试：新增伙伴系统模块并接入现有物理内存管理框架；覆盖块分裂、伙伴合并、内存不足和非二次幂页数请求。

## 实现 Prompt

```text
请在现有 RISC-V ucore Lab2 物理内存管理框架中新增 Buddy System 分配器，
保持 struct pmm_manager 接口不变，不修改 default_pmm_manager 和 Best-Fit
模块的实现或框架测试。

要求：
1. 新增 kern/mm/buddy_system_pmm.h 和 kern/mm/buddy_system_pmm.c，导出
   buddy_system_pmm_manager，并实现 init、init_memmap、alloc_pages、
   free_pages、nr_free_pages、check。
2. 用每个阶的空闲链表管理 2^order 页块；PageProperty 标记空闲块头，
   property 保存块页数，nr_free 始终等于所有空闲块页数之和。
3. init_memmap 接收任意连续页数，并按地址和长度自然对齐地拆成若干
   二次幂空闲块。
4. alloc_pages(n) 对任意 n>0 工作：向上取整到二次幂，优先寻找恰好大小，
   没有时从更高阶块逐层分裂；完整块不足时返回 NULL。
5. free_pages(base, n) 使用与分配时相同的 n 计算阶，并通过物理页号异或
   找到伙伴；仅当伙伴同阶、完整、空闲且未保留时合并，并继续向上合并。
6. 在 Makefile 中增加 PMM=buddy 构建选择，在 pmm.c 中选择对应 manager。
   保持 first_fit 和 best_fit 选项可用。
7. 不改动团队 page_init、First-Fit 和 Best-Fit 的既有语义；使用交叉编译和
   QEMU 在真实内核路径中验证，不以宿主模拟代替集成测试。
```

## 测试 Prompt

```text
请为已接入的 Buddy System 设计并执行可重复的 QEMU 集成测试。测试应使用一个
足够大的、连续且对齐的页区域建立独立分配器状态，避免依赖整机内存碎片；测试
结束后恢复原空闲链表和计数，不改变原有框架测试。

必须覆盖：
1. 分裂：分配 8 页和 8 页，得到相邻且各自 8 页对齐的块，空闲页数正确减少。
2. 合并：按顺序释放这两个 8 页块，验证合并为 16 页块，并能继续与另一块
   合并为 32 页块；空闲页数恢复。
3. 非二次幂：请求 3 页，实际使用 4 页阶块，返回地址按 4 页对齐，释放后
   完整合并回 32 页。
4. 内存不足：请求“当前空闲页数 + 1”时返回 NULL，空闲页数不变。
5. 一致性：检查每个阶链表中的块头、PG_property、property 大小、地址对齐、
   边界和空闲页计数总和。

运行 make check-buddy（内部执行 make clean、make PMM=buddy 和 QEMU）。
成功输出必须包含 Buddy 四类 PASS 和 check_alloc_page() succeeded!；若 panic，
应保留完整串口日志并定位到具体断言。
```

## 本次实际使用记录

先确认团队 `lab2` 分支中的 `page_init()`、构建选择器和测试注入方式，再把 Buddy 作为独立 manager 接入。实现时额外处理了初始空闲区不是单一大块的情况：将任意起止范围拆成对齐的二次幂块，否则 Buddy 的异或伙伴判定在非对齐范围内会失效。

测试使用真实分配器先取得 32 页区域，再在区域上重建独立的 Buddy 状态。这样分裂和合并的顺序是确定的；完成后恢复保存的链表状态并将 32 页归还给全局分配器，避免测试本身改变后续内核状态。

实际执行 `make check-buddy` 后，QEMU 串口输出依次出现 init_memmap、split、coalesce、非二次幂请求和内存不足五项 PASS，并以 `check_alloc_page() succeeded!` 结束。另用 `PMM=first_fit` 完成编译和 QEMU 回归，确认选择器没有破坏团队原有路径。原始片段见 `logs/buddy-qemu.txt`。
