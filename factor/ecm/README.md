椭圆曲线分解
============

ECM 找因子的期望代价是 $L_n[1/2, \sqrt{2}]$——与数域筛的 NFS 同一个量级，但能并行到任意多核，且**没有多项式选择与筛选**这些难并行、难调参的阶段。所以在几十到一百多位的范围内，ECM 是首选；再往上要靠 NFS。

目录里有三套 ECM 加上 P−1/P+1 的几个变体，按适用规模分工。

| 文件 | 规模 | 说明 |
| --- | --- | --- |
| `tinyecm.cpp` | 任意大数 | yafu 自有的完整实现，速度接近 GMP-ECM |
| `avxecm.cpp`、`avx_ecm_main.cpp` | 任意大数 | AVX-512 版，Montgomery 乘法用 52 位 limb 向量化 |
| `microecm.cpp` | 任意大数 | 简化版，主打低内存占用 |
| `ecm.cpp` | 任意大数 | 直接调用系统装的 GMP-ECM（`libecm`） |
| `vecarith52.cpp` 等 | — | AVX-512 的 52 位 limb 大数运算底座，上面几份共用 |
| `pm1.cpp`、`pp1.cpp` | 任意大数 | Pollard p−1、Williams p+1。原来还有一个 p−1 的 AVX 版 `avxppm1.c`，它不参与构建，已随遗留 C 文件一并删除 |


原理
----

给定曲线 $E: y^2 = x^3 + ax + b$ 与模数 $n$（$n$ 可以是合数），取曲线上一点 $P$，逐点倍点得到 $P, 2P, 4P, \dots$。设 $q$ 是 $n$ 的素因子。

- 在模 $q$ 上算，$E$ 的阶 $\#E(\mathbb{F}_q)$ 整除 $k!$ 之前都是正常的。
- 在模 $q^2$ 上算，一旦 $k$ 达到 $\mathrm{ord}_q(P)$，"模 $q$ 的点"退化为模 $q^2$ 的无穷远点，
  而模 $n$ 上不发生这件事。

于是取 $q$ 阶的倍数（一般用 $k = 210$，$210!$ 覆盖绝大多数 $q \le 10^6$），计算 $k!P = \mathcal{O} \pmod{q^2}$，在模 $n$ 上得到某个非无穷远点 $P'$，满足 $P' \equiv \mathcal{O} \pmod q$ 但不是 $\pmod{q^2}$。此时 $P'$ 的 $x$ 或 $y$ 坐标有一个分量是 $q$ 的倍数，取 $\gcd$ 即可：

$$\gcd(x, n) = q,\quad \gcd(y, n) = q,\quad \text{或}\quad \gcd(x^2 - y^2, n) = q$$

用 Montgomery 曲线（$y^2 = x^3 + A x^2 + x$，$A = 2 + \frac{s^2}{2}$）可以省掉模逆：倍点公式全用加法与乘法。这正是 `vecarith52*.c` 要提供的东西—— ECM 的耗时几乎全在曲线上做标量乘，标量乘的耗时几乎全在大数乘法上。

代价估计：对最小素因子 $q$，成功的概率约为

$$\Pr[\text{成功}] \approx \frac{\mathrm{ord}_q(P)}{\#E(\mathbb{F}_q)} \cdot
\frac{r!}{q^{r/\left(\frac{\log q}{\log B}\right)}}$$

其中 $B$ 是阶段一界、$r$ 是拐点。标准做法是分阶段（B1 之前用 Montgomery ladder，之后用窗口 + 预计算），把 $B_1$ 提到 $q^{1/2}$ 附近仍不显著变慢。


P−1 与 P+1
-----------

若 $q - 1$ 全部因子 $\le B$，则 $a^{B!} \equiv 1 \pmod q$，取 $\gcd(a^{B!} - 1, n)$ 即得 $q$。P+1（Williams）改用 $q + 1$ 光滑的素数。两者都比 ECM 便宜得多，但只在因子特别光滑时有效，且 $B$ 的选取是个两难：$B$ 太小覆盖不到因子，太大则单次代价线性增长。yafu 里通常作为 ECM 之前的廉价预处理，以及在已知因子大小的情况下直接出结果。


三套实现的取舍
--------------

`ecm.cpp` 走系统 GMP-ECM，是最省事的一条路，也是当前默认。`avxecm.cpp` 是 yafu 自研的 AVX-512 实现，用 Intel 的 52 位 limb 指令（`_mm512_sbb_epi52` 一族）在 8×64 位里塞进 8 个 52 位数，Montgomery 乘法完全向量化。`tinyecm.cpp` 是标量的自有实现，在没有 AVX-512 的机器上比 GMP-ECM 略慢，但可控、且不依赖外部库。

ECM 无条件编译进本项目：`config.mk` 里不再有开关，缺 `ecm.h` 会直接停止构建并说明该装什么。
