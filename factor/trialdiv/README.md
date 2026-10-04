试除与一维分解
================

`factor/trialdiv/` 里的方法复杂度都在 $O(n^{1/4})$ 以内，单机、确定（或几乎确定），
不需要大数库，适合小因子和几十位以内的数。规模再大就该换 `factor/ecm/`、
`factor/siqs/` 或 `factor/nfs/`。

| 文件 | 方法 | 计算量 |
| --- | --- | --- |
| `trialdiv.c` | 试除、素性判定的前置筛 | $O(\sqrt{p}/p)$ |
| `rho.c` | Pollard rho、Pollard p−1、Williams p+1 的小规模版本 | $O(\sqrt{p})$ |
| `squfof.c` | Shanks 的 SQUFOF | $O(p^{1/4})$ |
| `LehmanClean.c` | Lehman 方法 | $O(n^{1/3})$ |
| `tinyfactor.c` | 64 位以内的快速试除 | $O(\sqrt{p})$ |


试除
----

对每个 $p \le B$ 检查 $n \bmod p$。设最小素因子为 $q$，则最坏情况要做 $\sqrt{q}$ 次
取模，$B = \sqrt{n}$ 时是 $O(n^{1/4})$。实际用整块筛法：把 $B$ 以内的素数一次性筛出
（`factor/shared/ysieve/`），再逐个试除，比逐个试 $2,3,5,\dots$ 快一个量级。
这是所有分解方法的第一步——ECM、二次筛、数域筛都只在"剩下的部分"上工作，
所以这个阶段要把小因子尽量除干净。


Fermat 分解
-----------

两个素数 $p \le q$ 接近时有效。从 $a = \lceil\sqrt{n}\rceil$ 起找 $a^2 - n$ 的平方根：

$$b^2 = a^2 - n \quad\Longrightarrow\quad n = a^2 - b^2 = (a-b)(a+b)$$

$p, q$ 越接近，$\sqrt{n}$ 越靠近 $p$，需要的步数越少。步数约为
$\frac{(p+q)/2 - \sqrt{pq}}{2}$，在 $p, q$ 相差 $O(n^{1/4})$ 时是 $O(1)$。
适合 RSA 里两个素数由同一个种子生成的情形。实现里加了一段取模筛选，
只对 $a^2 - n$ 在模若干小素数下可能是平方的 $a$ 值做完整的开方测试。


Pollard rho
-----------

在模 $n$ 下迭代 $x \leftarrow x^2 + c$。序列必然进入循环，环长 $\lambda$ 与 $n$ 的
最小素因子 $q$ 同阶时，模 $q$ 与模 $n$ 的轨迹在期望 $O(\sqrt{q})$ 步内分叉：

$$\gcd\bigl(\prod (x_i - x_j),\ n\bigr) \ne 1$$

用 Brent 的改进（批量 gcd）把每次 $O(\log n)$ 的 gcd 摊薄成 $O(1)$。
$x^2 + c$ 上模 $q$ 的随机游走期望 $\sqrt{q}$ 步撞上，所以找最小素因子是
$O(n^{1/4})$。对完全平方数会退化（$x_i = x_j$ 恒成立），要先开方判掉。

Pollard p−1 是同一套框架换生成元：若 $q - 1$ 的因子都 $\le B$，则
$a^{B!} \equiv 1 \pmod q$ 而在模 $q^2$ 下一般不成立，取 $\gcd(a^{B!} - 1, n)$
即得 $q$。比 rho 快的前提是 $q$ 光滑；$q - 1$ 有大素因子时失败。


SQUFOF
------

Shanks 的平方形式因子分解。只在 $\lfloor\sqrt n\rfloor$ 附近找形如
$(a + \sqrt n)(b + \sqrt n) = c$ 的二次型，满足

$$\lfloor 2 (b - a) \sqrt n \rfloor + (a + b)^2 = c^2$$

形式序列在有限区域内周期重复，周期长度 $\Theta(n^{1/4})$。周期末尾的
$\lfloor (b + a)/\sqrt n \rfloor$ 是 $n$ 的一个因子。整个过程只用加减与平方根，
在 $\Theta(n^{1/4})$ 空间内完成——这是它相对 rho 的优势。

只用 32 位字长实现，$n$ 大到需要 64 位字长时由上层先做一次 rho 或试除。


Lehman 方法
-----------

Lehman 证明了任意奇合数 $n$ 都可写成 $n = k \cdot a^2 - b^2$。对每个
$k \le n^{1/3}$，令

$$a = \sqrt{\lceil n/k \rceil},\qquad a^2 \equiv n/k \pmod k \iff a^2 k \equiv n \pmod k$$

若 $\lfloor\sqrt{n/k}\rfloor \ne \lfloor\sqrt{n/k - a^2}\rfloor$ 附近出现
$a^2 k - n = b^2$，则 $n = k(a^2) - b^2$ 给出一个因子。对 $n$ 取立方根以内的
$k$ 各做一次平方根测试，$O(n^{1/3}\log n)$ 次开方。比 SQUFOF 的 $n^{1/4}$ 差，
但不需要大数运算且实现在纯 32 位字长下。
