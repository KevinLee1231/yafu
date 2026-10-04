外部格点筛（lasieve）
====================

NFS 到筛选阶段不自己筛，而是调用一批独立进程。这个目录就是那些进程的实现，
各自是一个静态链接的可执行程序，**不链接 yafu 的任何库**，只用 GMP。

`make all` 会一并构建它们（`all: yafu lasieve`），因为 yafu 按名字调用这些
可执行文件，缺了它们 yafu 什么 NFS 数也分解不了。单独构建仍可用
`make lasieve`，产物在 `bin/local/`。

yafu 通过 `yafu.ini` 里的 `ggnfs_dir` 找到这些可执行文件，默认指向
`factor/nfs/lasieve/bin/local/`，与 `make lasieve` 的输出目录一致。启动走
`fork`+`execv` 直接传 argv，不经过 shell。


为什么是独立进程
----------------

I 值（11..16）是**编译期**常量：`recurrence6I%d.o` 用 `-DI_bits=%*` 编译，
`asm/liblasieveI%d.a` 里的筛内核也由 m4 按 I 生成不同的立即数。

这六个库定义的是**同名符号**——`lasched`、`lasched0`、`slinie`、`medsched`、
`tdslinie` 等等，I11 和 I16 的符号表逐个对得上，代码内容不同而已。所以
它们无法同时链进一个二进制，而 yafu 正是要同时用 6 个不同 I 值并行筛
（`nfs_sieving.c` 的线程池，每个线程一个 I）。

要让它们进同一个进程，得给全部手写汇编做按 I 的符号改名，那是另一件
量级的事。所以这里保持独立进程：yafu 的线程池负责并行，每个线程 exec 一个
对应 I 值的 siever。


每个 siever 实例的状态
----------------------

ECM 与 P−1 的缓存（素数位图、加法链、B1/B2 scheme、montgomery 预计算）原先
是文件级全局，一个进程只有一份。现在在 `include/lasieve_ctx.h` 的
`lasieve_ctx` 里，每个实例一份；`lasieve_ctx_current()` 是 thread-local
访问路径，只是访问方式，状态本身按实例持有。迁移过程和理由见提交
`8f6bc2d` 与 `1872cec`。

`ctx_test` 验证隔离性质，接在 `make test-standalone` 里。注意它**不覆盖**
ECM/P−1 的数值正确性——那两条路径在迁移之前就有堆损坏，未修改的版本同样
复现，仓库里也没有任何测试走到它们。筛法数值由
`test/standalone/lasieve/sieve_oracle.sh` 的端到端基准守着。


目录结构
--------

| 位置 | 内容 |
| --- | --- |
| 顶层 36 个 `.c` | 主程序 `gnfs-lasieve4e.c`（另有 4f/4g 变体），以及它用到的各个部件 |
| `include/` | 本层头文件，含 `lasieve_ctx.h` |
| `asm/` | 59 个手写汇编/C 内核，编成 `asm/liblasieve.a` 与 `asm/liblasieveI%d.a` |
| `asm/include/` | 汇编层的 10 个头文件 |
| `asm/w_files/` | `wgen` 源，生成 `asm/` 下的部分 `.c`/`.h` |
| `w_files/` | `wgen` 源，生成顶层 11 个 `.c` |
| `bin/` | 构建输出，不入库 |


哪些是生成的
------------

`wgen` 是一个把 `.w` 展开成 C 的工具。**`.w` 才是要改的地方**，对应生成的
`.c`/`.h` 改了就白改。顶层 11 个文件是从 `w_files/` 生成的：

    gmp-aux.c        gnfs-lasieve4e.c   if.c                input-poly.c
    la-cs.c          lasieve-prepn.c    mpz-ull.c           primgen32.c
    recurrence6.c    redu2.c            strategy.c

其余 25 个是手写的，包括全部 MPQS、ECM、P−1 实现、`lasieve_ctx.c`
（实例状态）和几个测试程序（`ctx_test.c`、`ecmtest.c`、`mpqstest.c`、
`mpqs3test.c`、`mpqsstat.c`、`ecmstat.c`、`pm1stat.c`、`pm1test.c`）。

`w_files/primgen64.w` 是孤儿，没有对应的 `primgen64.c`。


关于 asm/
---------

`asm/` 里的 10 个头文件、59 个内核，**全部只被本目录使用**——yafu 的
ECM、二次筛、数域筛各有自己的 SIMD/汇编实现，与这里没有共享。所以它留在
lasieve 内部，不进 `factor/shared/`。

汇编直接读 C 的全局，约 57 个符号（`mpqs_sievelen`、`mpqs3_*`、`modulo32`、
`modulo_n`、`montgomery_inv_n` 等），汇编按符号名寻址，没有间接层。要让这些
也按实例分开，得给汇编加寄存器传参；`montgomery_modulo_n` 与
`montgomery_inv_n` 单独就在 30 个汇编文件里被引用。


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
