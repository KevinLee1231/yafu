外部格点筛（lasieve）
====================

NFS 到筛选阶段不自己筛，而是调用一批独立进程。这个目录就是那些进程的实现，
各自是一个静态链接的可执行程序，**不链接 yafu 的任何库**，只用 GMP。

`make all` 会一并构建它们（`all: yafu lasieve`），因为 yafu 按名字调用这些
可执行文件，缺了它们 yafu 什么 NFS 数也分解不了。单独构建仍可用
`make lasieve`。

产物与 yafu 可执行文件同目录（仓库根）。yafu 从自身可执行文件的位置找到它们
（Linux 读 `/proc/self/exe`），不依赖当前工作目录，也不需要配路径指回源码
树。

为什么是独立进程
----------------

I 值（11..16）是**编译期**常量：`recurrence6I%d.o` 用 `-DI_bits=%*` 编译，
`asm/liblasieveI%d.a` 里的筛内核也由 m4 按 I 生成不同的立即数。

这六个库定义的是**同名符号**：各 30 个，I11 与 I16 的符号表逐个对得上，代码
内容不同（`laschedI11.o` 与 `laschedI16.o` 的 md5 就不同）。一起链接时
`ld` 直接报 `multiple definition of 'lasched_1'`。

合并成一个二进制是可行的——机制就是按 I 给符号加后缀，用
`-Dlasched=laschedI11`（C 侧预处理按 token 替换，`lasched` 不会波及
`lasched0`；m4 侧 `-D` 就是定义宏），汇编与 C 在同一编译单元内一起改名。
yafu 需要 6 个不同 I 并行筛，所以合并之后 yafu 在**一个进程**里跑 6 个
siever 才是最终目标。

### 合并的确切范围

下面是从 `nm` 符号表实测出来的，不是估计。**106 个符号**要加 I 后缀：

| 来源 | 个数 | 谁定义 |
| --- | --- | --- |
| `liblasieveI%d.a` 的筛内核 | 30 | `asm/lasched.c`、`medsched.cpp`、`search0.cpp`、`slinie*.asm`、`tdslinie*.asm` |
| `gnfs-lasieve4e.c` 的文件级全局（含 `main`） | 15 | 同名 C 源 |
| `recurrence6.cpp` 的文件级全局 | 2 | 同名 C 源 |
| **汇编按名读取的 C 全局** | **59** | `mpqs.cpp` 18 个、`mpqs3.cpp` 21 个、`mpqs_gauss_*` 9 个、`modulo32`/`modulo64`、`montgomery_*` 6 个 |

后三类里，`main` 改名为 `mainI<N>` 之后由一个 `lasieve_run(I, argc, argv)`
分派；`lasieve-prepn.c` 与 `strategy.cpp` 是仅有的两个调用了改名符号的共享
对象，必须也按 I 各编一份。

最后一类是最容易被低估的：`asm/liblasieve.a`（39 个对象）**定义**
`montgomery_*`、并按名**读取**那 59 个，所以它自己也要按 I 编六份
（`libcoreI%d.a`）。只改 `liblasieveI%d.a` 是不够的——那样两个实例会共用
同一份 montgomery 状态和同一份 factor base。

改完之后每个 I 值拥有全套私有状态（FB、montgomery、ECM/P−1 缓存、
strategy 统计量），这正是进程内并行需要的。

### 验收

`sieve_oracle.sh` 对六个 I 值各跑一遍，关系集合必须与现在这六个独立可执行
文件逐字节一致。基准已经支持传入 siever 路径，改造后逐个 I 值比对即可。


每个 siever 实例的状态
----------------------

ECM 与 P−1 的缓存（素数位图、加法链、B1/B2 scheme、montgomery 预计算）原先
是文件级全局，一个进程只有一份。现在在 `include/lasieve_ctx.h` 的
`lasieve_ctx` 里，每个实例一份；`lasieve_ctx_current()` 是 thread-local
访问路径，只是访问方式，状态本身按实例持有。迁移过程和理由见提交
`8f6bc2d` 与 `1872cec`。

两项验证接在 `make test-standalone` 里：

- `ctx_test`：两个实例的字段互不可见，四个线程各持一个实例并发读写。
- `ecm_pm1_test`：ECM 与 P−1 能不能真的把已知合数拆开；以及两个实例用不同
  B1/B2 交错推进曲线，结果必须与各自单独跑一致。

它的两个实例**共用同一个输入**，这是有意的，原因见"关于 asm/"一节。

ECM 与 P−1 的位图访问曾经按字节下标算而缓冲区按 u64 字分配，越界到缓冲区
外 8 倍处，两条路径各一份，第一条曲线就崩（`5d592c7`）。这两条路径原先
没有任何测试走到，所以一直没被发现。筛法数值由
`test/standalone/lasieve/sieve_oracle.sh` 的端到端基准守着。


目录结构
--------

| 位置 | 内容 |
| --- | --- |
| 顶层 38 个 `.c` | 主程序 `gnfs-lasieve4e.c`（另有 4f/4g 变体），以及它用到的各个部件 |
| `include/` | 本层头文件，含 `lasieve_ctx.h` |
| `asm/` | 59 个手写汇编/C 内核，编成 `asm/liblasieve.a` 与 `asm/liblasieveI%d.a` |
| `asm/include/` | 汇编层的 10 个头文件 |
| `asm/w_files/` | `wgen` 源，生成 `asm/` 下的部分 `.c`/`.h` |
| `w_files/` | `wgen` 源，生成顶层 11 个 `.c` |
| `bin/` | 旧的输出目录，已不再使用（产物改到仓库根） |


哪些是生成的
------------

`wgen` 是一个把 `.w` 展开成 C 的工具。**`.w` 才是要改的地方**，对应生成的
`.c`/`.h` 改了就白改。顶层 11 个文件是从 `w_files/` 生成的：

    gmp-aux.c        gnfs-lasieve4e.c   if.c                input-poly.c
    la-cs.c          lasieve-prepn.c    mpz-ull.c           primgen32.c
    recurrence6.c    redu2.c            strategy.c

其余 27 个是手写的，包括全部 MPQS、ECM、P−1 实现、`lasieve_ctx.cpp`
（实例状态）和几个测试程序（`ctx_test.cpp`、`ecm_pm1_test.cpp`、`ecmtest.cpp`、
`mpqstest.cpp`、`mpqs3test.cpp`、`mpqsstat.cpp`、`ecmstat.cpp`、`pm1stat.cpp`、
`pm1test.cpp`）。

`w_files/primgen64.w` 是孤儿，没有对应的实现（CTANGLE 时代曾由 `primgen64.c` 生成，该 C 文件已随遗留 C 文件一并删除）。


关于 asm/
---------

`asm/` 里的 10 个头文件、59 个内核，**全部只被本目录使用**——yafu 的
ECM、二次筛、数域筛各有自己的 SIMD/汇编实现，与这里没有共享。所以它留在
lasieve 内部，不进 `factor/shared/`。

汇编直接读 C 的全局，约 57 个符号（`mpqs_sievelen`、`mpqs3_*`、`modulo32`、
`montgomery_*` 等），汇编按符号名寻址，没有间接层。要让这些也按实例分开，
得给汇编加寄存器传参；`montgomery_modulo_n` 与 `montgomery_inv_n` 单独就在
30 个汇编文件里被引用。

其中最要紧的是 **montgomery 状态**。`set_montgomery_multiplication()` 写的
`montgomery_ulongs`、`montgomery_inv_n`、`montgomery_modulo_n`、R2/R4 还是
进程级全局，而 ECM/P−1/MPQS 的每一次模乘都经过它们。后果是可观察的：
两个 ECM 实例**用不同的 N** 交错推进曲线时，先建立的那个实例的曲线会跑在
后者的 montgomery 状态下，算出错误的因子。`ecm_pm1_test` 因此让两个实例
共用一个输入——同一份 montgomery 状态，交错才有意义——并在文件头写明了
这一点。

所以当前 `lasieve_ctx` 覆盖的是 ECM/P−1 的缓存，**不覆盖**它们的模乘状态。
要把后者也按实例分开，就是那件纯汇编的活。


与 yafu 其余部分的关系
----------------------

顶层有一批部件在 yafu 别处也有对应实现：MPQS（`factor/mpqs/`）、
ECM 与 P−1（`factor/ecm/`）、素数筛（`factor/shared/ysieve/`）、
批量分解（`factor/shared/include/batch_factor.h`）。

它们看起来重复，但**不能直接引用**：lasieve 不链接 yafu 库，只依赖 GMP。
这些代码继承自更早的 mpqs4linux/gnfs4linux 一路，与 yafu 侧的版本在数据
结构上已经分叉（最明显的是 `batch_factor.h`：yafu 那份为 SIQS 的双多项式
关系加了 `signed_offset`/`success`/`lp_r[]`/`lp_a[]`，lasieve 这份没有），
硬合并不只是改 include，而是一次真正的移植。
