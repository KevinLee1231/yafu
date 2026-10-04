自初始化二次筛
==============

SIQS（Self-Initializing Quadratic Sieve）筛的是 $y(x) = x^2 - N$。期望代价
$L_N[1/2, 1]$，与 NFS 同量级；相对 NFS 的优势是没有多项式选择、矩阵也小得多，
劣势是对 $N$ 的代数结构没有要求之外的优势——NFS 遇到 $N = a^m \pm b^n$ 这类
特殊形式会快好几个数量级。

出处：S. Contini, *Factoring Integers with the Self-Initializing Quadratic Sieve*
(1997)；更早的分布式实现见 Alford 与 Pomerance, *Implementing the Self Initializing
Quadratic Sieve on a Distributed Network* (1993)。

计算器命令 `siqs(n)`。约 105 位以内是它的甜区，再往上参数没有充分调过。
另有面向小输入的精简版 `tinySIQS.c`，命令 `tinysiqs(n)`，不落盘、不能断点续算。


一、乘子与首项系数
-----------------

要筛的每个值都要求 $y(x) \equiv 0 \pmod p$ 且与 $N$ 互素，$p$ 遍历因子基
$B$ 以内的素数。取乘子 $k$ 使 $kN$ 有很多小的平方因子（$kN$ 的
$a \mid kN$ 且 $a$ 的所有素因子 $\le B$ 的平方因子数越多越好），
分解时再除掉 $k$。乘子把 $B$ 往上推，等价于加大筛法区间。

多项式写成

$$Q(x) = (A x + B)^2 - kN, \qquad B^2 \equiv kN \pmod A$$

写成 $B^2 - kN = AC$，则 $Q(x) = A (A x^2 + 2Bx + C)$。$A$ 若是平方
（$A = a^2$），只需筛 $A x^2 + 2Bx + C$，$Q$ 里的 $A$ 是完全平方所以自动光滑。
"自初始化"指的就是这个构造：$A$ 由因子基里的小素数凑出，凑好后筛法可以无缝
接着做下一个多项式，不必为每个多项式重新预热。代价是 $A$ 的凑法受限，
这正是 `factor/mpqs/` 里那套要换掉的东西。


二、筛法
-------

对每个素数 $p \le B$，解 $Q(x) \equiv 0 \pmod p$ 得两个根 $r_1, r_2$，
在筛数组的 $r_1, r_2, r_1 + p, r_2 + p, \dots$ 上加 $\lfloor \log_p B \rfloor$。
实现上用分段线性筛：一整段 $x$ 区间一次筛完。

- `med_sieve_32k*.c` / `med_sieve_64k.c`：中号筛数组（32 KB / 64 KB），
  按指令集出多个版本（`_avx2`、`_sse4.1`、`_knl`）。
- `sieve_macros_*.h`：把上面的变体做成宏，同一份筛法逻辑编译出不同指令集版本。
- `large_sieve.c`：大素数变体。$p > B$ 的素数不参与筛，改为在试除阶段收集
  "只含一个 $p > B$" 的部分关系，按 $p$ 配对合并（cycle finding），能显著减少
  需要的完整关系数。

试除分三级，目的是把大部分非完全分解的值尽早丢掉：
`tdiv_small.c`（小素数）→ `tdiv_med_*.c`（中素数，SIMD）→ `tdiv_large.c`（大素数，
带哈希表）。还有 `tdiv_resieve_*.c`：用二分法在"部分分解"的值上反解出完整的
因子分解，避免对每个候选做完整试除。


三、多项式根的增量更新
----------------------

相邻多项式之间 $B$ 变化不大，$A$ 变一点点，$B \bmod p$ 的变化量对每个 $p$
可以预先算好，根的位置随之平移。`poly_roots*.c` 与 `update_poly_roots_*.c`
就是维护这个：换多项式时对整段筛数组做一次平移，而不是重新解 $2\pi(B/\ln B)$
个二次同余。`poly_roots_32k.c` / `poly_roots_64k.c` 按筛数组块大小分开，
`_avx2` / `_sse4.1` / `_knc` / `_knl` 是指令集变体。

这是 SIQS 相对朴素 MPQS 的主要常数因子来源：多项式切换的代价从
$O(\pi(B)\log B)$ 降到 $O(\pi(B))$ 的平移操作。


四、关系与线性代数
-----------------

一条关系是 $Q(x) = \prod p_i^{e_i}$，全部 $p_i \le B$（或含一个 $p > B$，
即部分关系）。把指数对 2 取模，得到矩阵 $M$ 的一行。要凑出
$X^2 \equiv Y^2 \pmod N$ 且 $X \not\equiv \pm Y$，就是找 $M$ 的非平凡零空间
向量。

- 关系数要多过因子基规模才能保证有依赖（行数 $\ge$ 列数 + 1）。
- 大素数变体用 cycle finding 把共享同一大素数的部分关系先合并成完整关系。
- 筛选（把明显无用的关系剔掉以压缩矩阵）在 `filter.c` 与
  `factor/shared/common/filter/`。
- 求解用 `factor/shared/common/lanczos/` 里的 Lanczos 稀疏矩阵分块求逆
  （msieve 那套，`.qo` 里是 `factor/siqs/msieve/` 的 yafu 移植版）。
  $N$ 到 100 位以上时矩阵太大，分块 Lanczos 的常数比朴素 Gauss 消元好很多。


五、保存与续算
-------------

关系收集是长任务，`siqs.c` 会把关系追加写进 `siqs.dat`，中断后重跑同一个
输入可以接着算。文件名由 `-qssave` 指定，默认 `siqs.dat`。这一点在
`top/docfile.txt` 的 `siqs` 段有说明。
