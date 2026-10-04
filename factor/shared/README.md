共享代码
========

`factor/shared/` 放被多个方法共用的代码，**每个子目录只保留一份**，不按方法复制。头文件在各子目录自己的 `include/` 里，由 Makefile 的 `-I` 指路—— 不要靠"编译器先找包含者所在目录"来解析，那只在同目录时成立。

| 子目录 | 内容 |
| --- | --- |
| `include/` | 全局头文件。被所有方法共用，暂不按方法拆分 |
| `arith/` | 定长大数算术：52 位与 64 位 limb 两种实现、Montgomery 乘法、模乘、FFT 乘法 |
| `arith/tfm/` | 三精度浮点（`tfm.h`），ECM 里定标因子的中间计算用 |
| `ysieve/` | 素数筛：分段线性筛、素数计数、素数表、偏移表 |
| `common/` | 线程池、线性代数、关系筛选、savefile、表达式求值 |
| `common/filter/` | 关系筛选：clique 收集、预筛、singleton/duplicate 区分、合并 |
| `common/lanczos/` | 稀疏矩阵分块求逆（msieve 那套，`.qo`） |
| `common/lanczos/cpu/` | Lanczos 的 CPU 矩阵乘法内核 |
| `common/lanczos/gpu/` | Lanczos 的 GPU 内核（CUDA） |
| `ytools/` | 杂项工具：CPU 探测、缓存大小、计时、线程封装、哈希 |
| `aprcl/` | APR-CL 素性判定（含 52 位与 32 位两套）、tinyprp |
| `cub/` | vendored 的 NVIDIA CUB 库与基于它的 GPU 碰撞/排序引擎 |


大数算术
--------

`arith/` 的 `mp_t` 是定长的、以 32 位为单位的数组（`arith.h`，`MAX_MP_WORDS` 个字）。定长带来两个好处：可以在栈上分配、不需要单独的分配器；乘法可以用固定形状的循环展开。代价是超过 `MAX_MP_WORDS` 就得进位到 GMP（`gmp_xface.h` 里的 `mp_t2gmp` / `gmp2mp_t`）。

`arith0.c` 到 `arith3.c` 按运算类型分：`arith.c` 派发，`arith1.c` 加减比较，`arith2.c` 乘除（Montgomery 形式），`arith3.c` GCD、模逆、位扫描。`limb1.h` 与 `limb2.h` 是两份 limb 布局（32 位与 52 位），`mp_platform.h` 定义平台相关的类型别名与内建函数包装。

`fftmul.c` 是大块的 FFT 乘法，用在 NFS 的多项式运算里。`vecarith52*.c` 在 `factor/ecm/` 下（52 位 limb 的 AVX-512 向量化实现），虽然物理上不在 `shared/`，但它服务的是 ECM 与 `arith/` 的同一类需求。


线性代数与筛选
--------------

`common/filter/` 与 `common/lanczos/` 是 SIQS 与 NFS 共用的两套后处理：前者压缩关系矩阵的列，后者解 $M$ 的零空间。SIQS 用的是 `factor/siqs/msieve/` 下 yafu 自己移植的一份（Lanczos 另有一套 yafu 移植版，见 `factor/shared/include/lanczos.h`），NFS 用的是这里 msieve 的原版（`include/ms_lanczos.h`）。两套并存是因为路径与数据结构都不同，硬合成一套的收益不抵改动风险。
