数域筛
======

NFS（Number Field Sieve）是目前已知的对任意大整数最快的通用分解算法，期望代价

$$L_N\!\left[\tfrac{1}{3},\ \bigl(\tfrac{64}{9}\bigr)^{1/3}\right]
= \exp\!\Bigl(\bigl(\bigl(\tfrac{64}{9}\bigr)^{1/3} + o(1)\bigr)
(\ln N)^{1/3}(\ln\ln N)^{2/3}\Bigr)$$

比二次筛的 $L_N[1/2, 1]$ 在渐近意义上快得多。大约 100 位以内二次筛仍更快（常数因子更小），180 位以上 NFS 才明确占优。

出处：C. Pomerance, *The Quadratic Sieve Factoring Algorithm* (1985)；通用形式见 A. K. Lenstra 与 H. W. Lenstra Jr. 1975 年的双域构造；Pomerance & Smith, *A Pipeline Architecture for Factoring Large Integers with the Quadratic Sieve Method*, SIAM J. Comput. 17 (1988) 387–403。

目录分工：`nfs.c` 一族是作业编排与后处理，`gnfs/` 是数域筛本体（主构建里编），`lasieve/` 是外部格点筛（**单独的子 Makefile**，不进主构建）。


一、两个多项式
--------------

取本原整数根 $\alpha$（通常取 1 到 $\lfloor\sqrt[3]{2M}\rfloor$ 之间的素数）与 $M = \lceil N^{1/3}\rceil$，找一个 $A$ 满足

$$F(x) = A x^2 + B x + C \equiv 0 \pmod M,
\qquad A \equiv 0 \pmod M, \qquad (B, M) = 1$$

于是 $F(\alpha) \equiv 0 \pmod M$，即 $F(\alpha) = kN$（$M^2$ 附近时 $k$ 是个位数）。同时取 $G(y) = y + M$，$G(\beta) = \beta + M$。在 $\mathbb{Z}[\alpha]$ 与 $\mathbb{Z}[\beta]$ 上分别对 $\alpha$ 附近的 $\alpha + v$ 与 $\beta + w$ 做同样的筛选，收集满足

$$F(\alpha + v) \equiv 0 \pmod d, \qquad G(\beta + w) \equiv 0 \pmod d
\qquad (d \le B, \ d \text{ 为素数})$$

的整数对 $(v, w)$，记 $r_d = v \bmod d$（从第一个多项式）与 $s_d = w \bmod d$（从第二个）。


二、关系与平方
--------------

理想情况下每个 $d$ 恰好贡献一次，得到

$$X = \prod_d d^{e_d} = \prod_d r_d, \qquad
Y = \prod_d d^{e_d} = \prod_d s_d$$

于是 $X \equiv Y \pmod N$（两个多项式都模 $N$ 为零）。但单个 $d$ 一般贡献零次或两次（$r_d$、$s_d$ 各一个根），所以要配对：把满足 $r_{d_1} = -r_{d_2}$ 的大素数 $d_1, d_2$ 配成 "cycle"，消掉奇数次出现的大素数，剩下的就是完整关系。

完整关系给出 $X \equiv Y \pmod N$，但还需要两边在 $\mathbb{Z}[\alpha]$ 上也是平方：

$$\left(\frac{X}{Y}\right)^{2} \equiv 1 \pmod N,
\qquad \left(\frac{X}{Y}\right)^{2} \equiv 1 \pmod 4M$$

$2 \times 2$ 个平方根给出四个候选 $(\pm X, \pm Y)$，取使 $\gcd(x - y, N) \notin \{1, N\}$ 的那一组。矩阵的零空间用 `factor/shared/common/lanczos/` 的 Lanczos 求（`gnfs/sqrt/` 另有开平方的代码）。


三、多项式选择
--------------

这是 NFS 最难调的部分，要在两个域上同时让 $|F(\alpha + v)|$ 与 $|G(\beta + w)|$ 都小。`gnfs/poly/` 里的评分大致是：

1. 收集候选的 $B$ 以内素数 $\{q\}$，取 $q \bmod M$ 的均方根 $\rho \approx \sqrt{M/\ln M}$。
2. 筛掉 $\left[\frac{2k^2}{M}\right] < \rho < \left[\frac{8k^2}{M}\right]$ 的 $q$。
3. 再筛 $\left[\frac{4k^2}{M}\right] < \rho < \left[\frac{16k^2}{M}\right]$（更强的 $\rho$ 条件）。
4. 按 $q \bmod 2M$ 的取值分桶，取某些与 $q'$ 配对后能同时满足 $q + q' \equiv 0 \pmod{2M}$
   的组合。
5. 对候选解算实际的小均值 $\sigma$，只保留 $\sigma$ 最小的若干个。

`gnfs-params-Gimarel.txt`、`gnfs-params-table.txt` 是两套现成的参数表（按 $\log_2 N$ 查表）。


四、筛选
--------

`gnfs/filter/`：把明显无用的关系剔掉以压缩矩阵。最主要的两条判据是

- **相对光滑性**：$|F(\alpha + v)|$ 的对数与素因子分布不匹配；
- **Gaussian 启发式**：$G(\beta + w)$ 在 $N$ 的二次剩余里出现的分布不对。

`factor/shared/common/filter/` 里是共用的实现（clique 收集、预筛、singleton / duplicate 区分、合并）。yafu 的 SIQS 也在 `factor/siqs/filter.c` 里用同一套做后处理（"post-processing is performed using a port of Jason Papadopoulos's msieve filtering"）。


五、lasieve
-----------

筛选阶段被单独拆成一个可执行文件（`factor/nfs/lasieve/`），因为它要跑很久且分布式的节点上只有这一个阶段在跑。输入是多项式与因子基，输出是关系文件。

它是 msieve 里 J. Papadopoulos 写的那套 Sieve，由本项目从零重写并扩展（`lasieve.h` 顶部的版本说明与 `top/docfile.txt` 里的 `lasieve` 段有描述）。相比 msieve 原版，yafu 的实现加入了：

- 更激进的截断（truncation）与素数次幂（prime power）的处理；
- 分块调度（`asm/lasched.h`、`asm/medsched.h`）与专用汇编乘法
  （`asm/montgomery_mul.h`）；
- AVX-512 辅助（`avx512_aux.h`）；
- 整批（batch）模式：把多个多项式一起筛，用不完的筛选预算重投到后面的多项式。

构建完全独立：`make lasieve` 会转到 `factor/nfs/lasieve/Makefile`，产出 `bin/local/` 下的 `gnfs-lasieve4I{11..16}e`。头文件在 `lasieve/include/` 与 `lasieve/asm/include/`，由子 Makefile 的 `-Iinclude -Iasm/include -I../include` 解析，头文件依赖在该 Makefile 里显式列出。`make test-standalone` 跑 `test/test_lasieve.sh` 的五个独立回归。


六、SNFS
-------

`snfs.c`：当 $N$ 有特殊形式（$a^m \pm b^n$）时取 $m$ 或 $2n$ 次域，多项式选择几乎不用调，能比 GNFS 快好几个数量级。Cunningham 表里那些 $a^n \pm b$ 的数走这条路。
