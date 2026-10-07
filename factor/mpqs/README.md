多项式二次筛（MPQS）
====================

MPQS（Multiple Polynomial Quadratic Sieve）是 Schroeppel 在 1973 年提出、R. D. Silverman 给出完整分析并正式命名的方案：R. D. Silverman, *The Multiple Polynomial Quadratic Sieve*, Math. Comp. 48 (1987) 329–339。

期望代价同为 $L_N[1/2, 1]$。与 SIQS 的区别在多项式怎么来：SIQS 靠 "自初始化"让筛法在多项式之间连续，MPQS 则老老实实为每个多项式重新起筛。工程上 MPQS 单多项式的摊销成本更高，但在高度并行的环境下更容易切分—— 每个核可以独立做一批多项式直到算完再通信。

计算器命令 `mpqs(n)`。代码是 msieve 的原版（Jason Papadopoulos），本项目只做了两件事：把它的头文件挡在边界外（`include/mpqs_xface.h`），以及把失败路径从 `exit()` 改成返回错误。


一、多项式的形式
----------------

筛

$$Q(x) = (A x + B)^2 - N, \qquad B^2 - N = A C$$

由 $B^2 \equiv N \pmod A$ 展开得

$$Q(x) = A\bigl(A x^2 + 2 B x + C\bigr)$$

当 $A$ 是平方时，$Q$ 里那个 $A$ 本身完全光滑，只需对 $A x^2 + 2Bx + C$ 判光滑。经典取法是让 $A$ 由因子基内若干小素数之积构成，并取 $A \approx \sqrt{2N}/S$（$S$ 是筛法区间长度）——离这个值越近，$Q(x)$ 的平均值越小，判光滑的概率越高。

`poly.cpp` 里 `poly_init` 做的就是给这些素数分配比特数：$A$ 需要 `conf->a_bits` 个比特，就从因子基里挑若干素数使它们的 log 刚好加到 $a_{\text{bits}}$，且每个素数的大小尽量接近以免某几个过大。


二、筛法与关系
--------------

对 $p \le B$ 解 $A x^2 + 2Bx + C \equiv 0 \pmod p$，用预计算的 $2^{32}/p$（小素数）或 $2^{40}/p$（大素数）做倒数乘法定位根。`sieve.cpp` 里这两段分别检查倒数表放不放得下，放不下就返回错误—— 这是我把 `exit()` 换掉的两处之一。

超过因子基的素数按大素数变体处理，哈希表记下"只含一个 $p > B$"的值，`sqrt.cpp` 的 `find_factors` 做 cycle finding 拼出完整关系，再从矩阵里取非平凡零空间向量（`gf2.cpp`），得到 $X^2 \equiv Y^2 \pmod N$，最后 $\gcd(X - Y, N)$。$X \pm Y$ 只需试一个：Pariser 的论证保证不会两个都平凡。


三、本项目做的改动
------------------

**头文件边界。** `mpqs.h` 会经 `ms_common.h` 拉进 msieve 的 `mp.h`，
把 `mp_t` 定成 32 字长；yafu 的 `core_types.h` 把同名 `mp_t` 定成 64。同名不同尺寸，无法同处一个编译单元。所以 yafu 侧只能通过 `mpqs_xface.h` 调用，那层门面把 msieve 的头文件挡在边界外，只讲 `mpz_t`。

**失败不再杀进程。** 原来 `poly.cpp` 与 `sieve.cpp` 里有四处 `exit(-1)`：
多项式选不出、因子位数凑不齐、两处倒数表越界。这些表示"这个输入 msieve 处理不了"，不是"程序不能继续"。挂到计算器上时，`mpqs(19 位输入)` 会把整个 yafu 杀掉，交互会话随之消失。现在 `poly_init` 与 `do_sieving` 返回 `int`，`factor_mpqs` 现有的空指针检查正好接住，命令会打印一行说明并让计算器继续。

**性能现状。** 本项目 vendored 的 `sieve_core.cpp` 只编了 generic SSE2 的
32 KB 与 64 KB 两个变体，msieve 上游的 AVX2/AVX512 筛法内核没有一起带进来。加上参数表在这个尺寸区间插值出的筛法区间偏小，21 位以上的输入在本机筛不完。`gf2.cpp` 里还有一处 `exit(EXIT_FAILURE)`（矩阵列溢出）没改，它要连带改 `solve_linear_system` 的签名，触发条件也与上面几处不同。
