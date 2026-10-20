# yafu 测试

回归分两层：**框架层**（`test/layer0..3`，链接进 `build/yafu_test`）和
**脚本层**（独立编译、跑真实可执行文件或子构建）。两层都由
`test/run_all.sh` 统一编排。

## 一条命令跑全部

```sh
make test-all              # 编译 yafu + 测试框架，然后跑所有回归
make test-harness          # 只验证回归驱动自己可信
sh test/run_all.sh         # 不重编，只跑（需要 yafu 与测试框架已就绪）
sh test/run_all.sh cli     # 只跑名字里含 cli 的套件
```

`make test-all` 逐套打印结果并给汇总；**任何一套非 0，整体非 0**，失败的套件
会把日志复制成 `test-failed-<名字>.log` 留在仓库根，方便事后看。

## 套件一览

| 名字 | 跑什么 | 入口 |
| --- | --- | --- |
| `build` | 编译 yafu | `make yafu` |
| `unit` | 分层单元测试，全部 | `./build/yafu_test` |
| `unit_fast` | 只跑标了 `fast` 的 | `./build/yafu_test --tag fast` |
| `full` | 第 0-3 层，含 SIQS 集成与计算器 | `make test-full` 后跑 `build/yafu_test_full` |
| `cli` | 命令行 / 管道 / 批处理 / 计算器 | `test/test_cli.sh` |
| `nfs` | NFS 文件拆分与批量因子树（独立编译） | `test/test_nfs.sh` |
| `lasieve` | 筛法器六个 I 值、并发隔离、stat/test 链接 | `test/test_lasieve.sh` |
| `harness` | 驱动自检 | `test/harness_selftest.sh` |

各套也可以单独跑：

```sh
make test-run              # 等价于 unit
make test-full-run         # 第 0-3 层，含 SIQS 集成与计算器
make test-cli
make test-standalone       # = nfs + lasieve 两套
make test-sanitize         # ASan/UBSan 下重建并跑
```

## 为什么要有 `harness`

一套永远绿的回归比没有回归更糟：它让人以为代码被覆盖了。2026-10-07 在这个
仓库里就撞上过——筛法器的 `check_sieve` 长期链接失败（`tdsieve_sched2buf` 只
剩声明、实现早随 `.asm -> .cpp` 迁移删掉了），脚本照样报成功，
`make test-standalone` 一直是绿的。

所以 `harness_selftest.sh` 会故意制造失败，确认驱动确实会报出来：子套件非 0
时驱动必须非 0、失败的退出码要原样传出、挑不出套件时不能静默通过。

## 分层测试写在哪

| 层 | 位置 | 覆盖 |
| --- | --- | --- |
| 0 | `test/layer0/` | 平台 128 位乘除、加减进位链 |
| 1 | `test/layer1/` | 模运算、Montgomery、素性判定、素数筛 |
| 2 | `test/layer2/` | ECM：小因子例程 |
| 3 | `test/layer3/` | SIQS 完整分解、计算器、参数与 INI 解析 |

框架在 `test/testkit.{h,cpp}`、`test_data.{h,cpp}`、`test_main.cpp`。每个
layer 文件导出一个 `tk_module`，由 `test_main.cpp` 汇总。加一个模块的步骤：

1. 在对应 `layerN/` 下新建 `test_<名字>.cpp`；
2. 定义 `static const tk_test tk_tests_<名字>[]` 和
   `const tk_module tk_module_<名字>`；
3. 在 `test/testkit.h` 里加 `extern` 声明；
4. 在 `test/test_main.cpp` 的模块表里加一行；
5. 在顶层 `Makefile` 的 `TEST_SRCS`（第 0-2 层）或 `TEST_L3_SRCS`（第 3 层）
   里加源文件路径。

断言用 `TK_CHECK` / `TK_CHECKF` / `TK_EQ_U64`（非中止，一次报出全部失败）和
`TK_REQUIRE`（当继续做没有意义时中止当前测试）。基准用 `TK_BENCH`，只在
`--bench` 下跑，不影响成败。RNG 由 `--seed` 决定且每个测试一条独立流，失败能
从打印出的种子复现。

## OpenCL 内核测试

`layer2/test_ocl_ecm.cpp` 在 GPU 上跑生产用的 OpenCL 内核
（`factor/core/opencl_tinyecm.cl`），把内核报出的每个因子都对着已知结构的
输入核对：

```
gbl_ecm     64 位 ECM，半素数 61..62 位
gbl_ecm96   96 位 ECM，半素数约 82..87 位（30..34 位的 p）
gbl_pm196   96 位 P-1，p-1 由 500 以内的互异素数构成
```

每个输入分三类：pass（报出真因子）、miss（没报出因子——ECM 是概率性的，
少量 miss 允许，不算失败）、hard（报出的数不整除输入——那才是真 bug）。
任何一个内核出现 hard 失败，或者 miss 率超过预算，这一项就失败。

没有 OpenCL GPU 时测试打印一条说明然后通过，所以带 `WITH_OPENCL=1` 编出来的
测试在没有 GPU 的机器上也能跑。

```
make WITH_OPENCL=1 test
./build/yafu_test --module ocl_ecm -v
```

- `YAFU_OCL_PLATFORM=<子串>` 按名字选平台；
  `YAFU_OCL_KERNEL_DIR=<目录>` 指向 `.cl` 文件所在目录（不��仓库根运行时用）。
- 第一次跑要编译内核，某些驱动上要几十秒（之后的运行由驱动缓存）。

## 退出码

各脚本统一成这样：

```sh
cleanup()
{
    rc=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$task_dir"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
```

信号各有各的 trap，不会再出现"收到 Ctrl-C 之后脚本从中断处继续跑并报通过"。
