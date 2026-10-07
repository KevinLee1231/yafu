调度层
======

`factor/core/` 本身不做分解，它决定每个输入走哪条路线，并提供命令行选项与 `yafu.ini` 的实现。这里的"数学"是各方法代价函数的比较——选方法等价于比较 $e^{\frac{1}{3}(\ln N)^{1/3}(\ln\ln N)^{2/3}}$（NFS）与 $e^{\frac{1}{2}\sqrt{\ln N\ln\ln N}}$（ECM、二次筛）在给定 $N$ 下的交叉点，再用实际机器上的常数因子修正。

| 文件 | 职责 |
| --- | --- |
| `autofactor.cpp` | 逐个输入决定分解路线：先试除，再按剩余大小与因子猜测选 ECM / SIQS / NFS |
| `batch_factor.cpp` | 一批数一起分解时的并行与余因子处理 |
| `factor_common.cpp` | 试除、余因子提取、跨方法共享的公共步骤 |
| `tune.cpp` | 读取 `yafu.ini` 里的交叉点参数，并自带的 `tune` 命令实测 |
| `prime_sieve.cpp` | 给试除用的素数表 |
| `gpu_cofactorization.cpp` | 把余因子分解整批丢给 GPU（CUDA）。OpenCL 的内核源 `opencl_tinyecm.cl`、`opencl_intrinsics.cl` 还在树里，但驱动端已不参与构建 |
| `cuda_tinyecm.cu`、`opencl_tinyecm.cl` | GPU 上的 tinyECM 内核 |


路线选择
--------

对每个输入：

1. **试除**（`factor/trialdiv/`）到 $B$，把能除干净的都除掉。
2. 剩下的是素数就直接给答案。
3. 剩下是小合数，用 `factor/trialdiv/` 里的 rho / SQUFOF / Lehman —— 单机、
   确定、几百位以内都够快。
4. 剩下的是中等大小的合数，比对 SIQS 与 NFS 的交叉点（`tune` 实测得到，
   存进 `yafu.ini`），选交叉点以下用 `factor/siqs/`、以上用 `factor/nfs/`。
5. 其中带已知因子结构的（$a^m \pm b^n$ 这类）优先走 SNFS。
6. 中间穿插 `factor/ecm/`：从用户指定的区间里跑 ECM 常能一次摘掉多个
   中等素因子，这也是为什么"已知 $N$ 有若干 15 位因子"时 ECM 远快于筛法。

`tune` 命令实测的就是第 4 步的交叉点：给定一批已知分解的数，分别用 SIQS 与 NFS 分解，取耗时相等的输入大小写回 `yafu.ini`。
