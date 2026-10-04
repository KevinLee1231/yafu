外部格点筛（lasieve）
====================

NFS 到筛选阶段不自己筛，而是调用一批独立进程。这个目录就是那些进程的实现，
各自是一个静态链接的可执行程序，**不链接 yafu 的任何库**，只用 GMP。
主构建完全不碰它们；`make lasieve` 转到本目录的 `Makefile` 单独构建。

yafu 通过 `yafu.ini` 里的 `ggnfs_dir` 找到这些可执行文件，默认指向
`factor/nfs/lasieve/bin/local/`，与 `make lasieve` 的输出目录一致。


目录结构
--------

| 位置 | 内容 |
| --- | --- |
| 顶层 35 个 `.c` | 主程序 `gnfs-lasieve4e.c`（另有 4f/4g 变体），以及它用到的各个部件 |
| `include/` | 本层 18 个头文件 |
| `asm/` | 59 个手写汇编/C 内核，编成 `asm/liblasieve.a` |
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

其余 24 个是手写的，包括全部 MPQS、ECM、P−1 实现和几个测试程序
（`ecmtest.c`、`mpqstest.c`、`mpqs3test.c`、`mpqsstat.c`、
`ecmstat.c`、`pm1stat.c`、`pm1test.c`）。

`w_files/primgen64.w` 是孤儿，没有对应的 `primgen64.c`。


关于 asm/
---------

`asm/` 里的 10 个头文件、59 个内核，**全部只被本目录使用**——yafu 的
ECM、二次筛、数域筛各有自己的 SIMD/汇编实现，与这里没有共享。所以它留在
lasieve 内部，不进 `factor/shared/`。


与 yafu 其余部分的关系
----------------------

顶层有一批部件在 yafu 别处也有对应实现：MPQS（`factor/mpqs/`）、
ECM 与 P−1（`factor/ecm/`）、素数筛（`factor/shared/ysieve/`）、
批量分解（`factor/shared/include/batch_factor.h`）。

它们看起来重复，但**不能直接引用**：lasieve 是不链接 yafu 库的独立可执行
程序，只依赖 GMP。这些代码继承自更早的 mpqs4linux/gnfs4linux 一路，与
yafu 侧的版本在数据结构上已经分叉（最明显的是 `batch_factor.h`：yafu 那份
为 SIQS 的双多项式关系加了 `signed_offset`/`success`/`lp_r[]`/`lp_a[]`，
lasieve 这份没有），硬合并不只是改 include，而是一次真正的移植。
