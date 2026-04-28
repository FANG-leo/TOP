# LBM 求解器性能优化报告 (Part 1 - Part 8)

## 第一部分：排除系统性干扰与建立纯净基准线 (Part 1)

在进入核心算法优化之前，首要任务是消除非算法层面的系统性噪音，以建立一个纯净、真实的性能基准线 [cite: optimism1.md]。为了剥离这些干扰，主要进行了以下清理工作：

1. **剔除 IO 与打印开销**：在基准测试期间完全禁用了结果文件输出（`output_filename = none`），并关闭了计时循环内的每步进度打印（`show_progress = 0`） [cite: optimism1.md]。
2. **消除单进程冗余操作**：针对单进程 (single-rank) 运行场景，跳过了不必要的 `MPI_Barrier` 全局同步调用，并直接返回了无意义的边界交换 (halo-exchange) 操作 [cite: optimism1.md]。
3. **修复无效写入**：禁用了在不输出文件时依然执行的文件头写入操作 [cite: optimism1.md]。

经过上述彻底的降噪处理，程序排除了所有的系统性等待与 IO 干扰 [cite: optimism1.md]。在该纯净状态下运行 20000 次迭代的基准测试，获得了以下 5 项核心基础性能数据 [cite: optimism1.md]：

* **耗时 (Elapsed time)**：242.5 秒 [cite: optimism1.md]
* **品质因数 (FOM)**：10.57 MLUPS [cite: optimism1.md]
* **CPU 利用率 (CPU utilization)**：99.7% [cite: optimism1.md]
* **每周期指令数 (IPC)**：2.52 [cite: optimism1.md]
* **L1 缓存未命中率 (L1 miss rate)**：3.20% [cite: optimism1.md]

---

## 第二部分：平衡态计算热点优化 (Part 2)

* **优化原因（性能分析）**：
    根据初始的 `perf` 热点分析，程序性能严重受限于碰撞端的调用链。其中，`compute_equilibrium_profile`（平衡态计算）占据了最大的自身耗时（Self Overhead），比例高达 34.00% [cite: perf1.md]，是程序中最核心的热点 [cite: perf1.md]。
* **优化过程**：
    1. **算法特化**：将通用的平衡态计算实现替换为 D2Q9 特有的基于分量的辅助处理 [cite: optimism2.md]。
    2. **消除重复工作**：在碰撞循环中，将密度、速度及其平方等变量由“每个方向计算一次”优化为“每个网格点仅计算一次”并全程复用 [cite: optimism2.md]。
    3. **剥离函数调用开销**：在碰撞内核中使用更紧凑的专用路径，移除了对辅助函数的重复调用 [cite: optimism2.md]。
* **优化结果**：
    本轮优化极其成功，之前霸榜的热点 `compute_equilibrium_profile(...)` 已经不再作为独立的主要耗时函数出现 [cite: perf2.md]。
    * **耗时 (Elapsed time)**：116.24 秒 [cite: perf2.md]（缩短约 52.07%）
    * **品质因数 (FOM)**：22.11 MLUPS [cite: perf2.md]（提升约 109.18%）
    * **IPC**：2.30 [cite: perf2.md]
    * **L1 缓存未命中率**：11.08% [cite: perf2.md]（由于计算开销降低，访存变得更加频繁）

---

## 第三部分：主碰撞内核算术与控制开销优化 (Part 3)

* **优化原因（性能分析）**：
    根据 Part 2 的结果，性能瓶颈上移至完整的主碰撞内核 `compute_cell_collision` [cite: optimism3.md]。
* **优化过程**：
    1. **内联平衡态公式**：改为直接在碰撞内核中利用闭式表达式原地展开并计算 9 个分布值 [cite: optimism3.md]。
    2. **单网格预计算**：将共享标量项提前，确保每个网格点只需计算一次 [cite: optimism3.md]。
    3. **重写 BGK 更新公式**：使用混合形式 `f_out = (1 - omega) * f_in + omega * f_eq` 以利于编译器调度 [cite: optimism3.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：103.30 秒 [cite: perf3.md]（相比 Part 2 缩短约 11.13%）
    * **品质因数 (FOM)**：24.90 MLUPS [cite: perf3.md]（提升约 12.62%）
    * **数据流传输 (Propagation)** 正式超越碰撞内核成为最大热点（50.05%） [cite: perf3.md]。

---

## 第四部分：数据流传输 (Propagation) 内核与内存访问优化 (Part 4)

* **优化原因（性能分析）**：
    `propagation` 占据了 50.05% 的自身耗时，其低效的三重通用循环带来了极大的分支预测压力和访存开销 [cite: optimism4.md]。
* **优化过程**：
    1. **引入无分支内部快速通道**：引入特化的 D2Q9 拉取式（Pull-style）内核 [cite: optimism4.md]。
    2. **隔离边界处理**：将带有密集检查的边界逻辑隔离，仅处理极小比例的网格边缘 [cite: optimism4.md]。
    3. **扁平化数组索引**：彻底移除 `Mesh_get_cell()` 抽象调用，直接使用扁平索引 [cite: optimism4.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：67.50 秒 [cite: perf4.md]（缩短约 34.65%）
    * **品质因数 (FOM)**：38.18 MLUPS [cite: perf4.md]（提升约 53.33%）
    * 瓶颈再次回传至碰撞内核（66.10%） [cite: perf4.md]。

---

## 第五部分：碰撞外层循环与抽象层扁平化优化 (Part 5)

* **优化原因（性能分析）**：
    主碰撞内核耗时占比反弹至 66.10%，性能损耗主要在于每次更新网格时的函数调用边界和重复的地址计算 [cite: optimism5.md]。
* **优化过程**：
    1. **引入裸指针内部辅助函数**：核心计算直接操作裸指针 [cite: optimism5.md]。
    2. **外层循环扁平化扫描**：利用预计算步长直接对 `mesh->cells` 进行一维索引寻址 [cite: optimism5.md]。
    3. **循环外层预计算**：将松弛常数计算剥离出循环 [cite: optimism5.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：55.75 秒 [cite: perf5.md]（缩短约 17.41%）
    * **品质因数 (FOM)**：46.85 MLUPS [cite: optimism5.md]（提升约 22.71%）

---

## 第六部分：打破内存墙 —— AoS 到 SoA 数据架构重构 (Part 6)

* **优化原因（性能分析）**：
    根本的架构级瓶颈在于“结构体数组” (AoS) 内存布局阻碍了跨网格的 SIMD 向量化，并导致不连续的内存访问 [cite: optimism6.md]。
* **优化过程**：
    1. **内存布局重构为 SoA**：物理内存重组为 9 个独立且连续的方向主平面 [cite: optimism6.md]。
    2. **重写核心热点内核**：显式地按平面进行存取，彻底消除了交错存取的惩罚 [cite: optimism6.md]。
    3. **建立非热点兼容层**：使用 `load_cell`/`store_cell` 确保非热点路径的兼容性 [cite: optimism6.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：35.84 秒 [cite: perf6.md]（缩短约 35.71%）
    * **品质因数 (FOM)**：72.73 MLUPS [cite: perf6.md]（提升约 56.71%）
    * **IPC** 回升至 2.32，L1 未命中率明显下降 [cite: perf6.md]。

---

## 第七部分：压榨硬件底座 —— 开启本地架构向量化 (Part 7)

* **优化原因（性能分析）**：
    SoA 布局已经为向量化扫清障碍，但保守的编译器默认设置生成的是 128 位指令 [cite: optimism7.md]。
* **优化过程**：
    1. **添加硬件特定编译标志**：添加 `-march=native` 编译器标志 [cite: optimism7.md]。
    2. **指令集升级**：核心循环成功升级到 256 位（AVX2）指令 [cite: optimism7.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：28.67 秒 [cite: optimism7.md]（相比 Part 6 缩短约 20.00%）
    * **品质因数 (FOM)**：91.17 MLUPS [cite: optimism7.md]（提升约 25.35%）

---

## 第八部分：数据流边界隔离与并行架构诊断 (Part 8)

* **优化原因（性能分析）**：
    `propagation` 的边界处理仍然带有昂贵的逻辑检查，阻碍了主干的流畅性 [cite: optimism8.md]。
* **优化过程**：
    1. **内外路径彻底分离**：内部网格保持高速通道，边界改为显式的直接平面赋值 [cite: optimism8.md]。
    2. **静止分布函数拷贝优化**：使用 `std::memcpy` 处理不需要位移的 `f0` 平面 [cite: optimism8.md]。
    3. **跨配置并行诊断**：分析 `np` 与 `omp` 组合下的扩展性 [cite: perf8.md]。
* **优化结果**：
    * **耗时 (Elapsed time)**：23.70 秒 [cite: optimism8.md]（单核峰值表现，相比 Part 7 缩短约 17.34%）
    * **品质因数 (FOM)**：109.62 MLUPS [cite: optimism8.md]（单核最终成绩，相比 Part 1 累计提升约 937%）
    * **并行诊断结论**：`np=2, omp=1` 是当前机器上的最优配置，证明盲目增加线程会导致严重的内存竞争 [cite: perf8.md]。
