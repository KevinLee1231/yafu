# yafu 变更日志

本项目自 2026-10-03 起脱离上游独立维护，版本号不再跟随 yafu 上游。

## 版本号规则

采用三段式 `主版本.次版本.修订号`：

| 段位 | 何时 +1 |
| --- | --- |
| 主版本 | 架构性改变：目录结构重排、构建系统重做、依赖项增减 |
| 次版本 | 新增分解算法，或既有算法新增实现路径（如 GPU 版本） |
| 修订号 | 修 bug、性能或健壮性改进，不改变外部行为 |

**版本号只有一个来源，就是本文件。** `factor/shared/include/yafu.h` 里的
`YAFU_VERSION_STRING` 永远等于这里的版本号，改版本时两处必须一起改。它决定
`yafu -v` 的输出和自动分解写进 `factor.json` 的 `"yafu-version"` 字段；对不上就
以本文件为准去改 `yafu.h`。

---

## 3.0.0

### AVX-512 成为必备项，不再有"通用档位"

SIQS 在上一轮收敛到只剩 AVX-512 一条内核路径之后，标量与 SSE4.1 的实现已经
删掉，但构建系统还留着 `generic` / `sse41` / `avx2` 三档。这三档现在会编出
一个链接不过的二进制：`siqs_select_kernels` 里引用的
`tdiv_medprimes_32k_avx2`、`resieve_medprimes_32k_avx512bw`、
`med_sieveblock_32k_avx512bw`、`lp_sieveblock_avx512bw`、
`check_relations_siqs_*_avx2`、`check_relations_siqs_16_avx512` 与
`nextRoots_32k_knl_*` 这些符号都只在 `USE_AVX512=1` 时才进编译，链接器会报
十六个 undefined reference。

本项目是自用的，只在本机（Zen 5）跑，不打算为跑不起来的机器保留降级路径。
所以：

- `USE_AVX512` 变成默认开启，并且是必备项——显式写 `USE_AVX512=0` 会被
  Makefile 直接拦下并说明原因，而不是让人拿到一个"构建成功"的坏产物。
- SIQS 那几个 ISA 源文件无条件参与编译，不再按 `USE_AVX512` 分支。
- `FORCE_GENERIC` 的含义收窄：它本来是"关掉全部 SIMD 路径"，现在只做一件事
  ——让 `qs_impl.h` / `lanczos.h` / `pmpqs.cpp` 里那几处 x86-64 汇编辅助例程
  改用可移植 C 实现。它不再动指令集档位。
- CI 矩阵从五档收敛到两档（`avx512`、`avx512ifma`），回归改在 `avx512`
  档位上跑（它对 CPU 的要求最低，托管 runner 一定支持）；`avx512ifma` 只编译
  不运行——runner 的 CPU 未必有 IFMA，跑起来会直接 illegal instruction。
- CI 的触发条件加上 `wsl-zen5-build-fixes` 分支。之前这个分支上的构建问题
  一直没人发现，就是因为工作流只在 push 到 master 或给 master 提 PR 时触发。

### 修掉全新检出编不出来

`Makefile` 里 `$(BUILD_CONFIG)` 以 `build/.deps/` 为 order-only 前置，但全树
没有为这个目录写规则。`build/.deps/` 存在时 make 认为前置已满足，于是一直
没人发现；树里没有 `build/` 目录时（全新检出，或者 `make clean` 之后），
构建会在第一条命令就停住：

```
make: *** 没有规则可制作目标“build/.deps/”，由“build/.deps/build-config” 需求。 停止。
```

补上目录规则即可。

### 把上游的 OpenCL 后端移植到新的目录布局

上游把 GPU 余因子分解的 OpenCL 后端做成了可以脱离 CUDA 工具链单独构建
（`WITH_OPENCL=1`），还配了内核级的测试。这些在旧目录布局里是
`common/ocl_xface.c`、`factor/gpu_cofactorization_cl.c` 与
`test/layer2/test_ocl_ecm.c`，rebase 之后要把它们按新布局搬到：

| 上游 | 本仓库 |
| --- | --- |
| `common/ocl_xface.c` | `factor/shared/common/ocl_xface.cpp` |
| `factor/gpu_cofactorization_cl.c` | `factor/core/gpu_cofactorization_cl.cpp` |
| `factor/gpu_cofactorization_cl.h` | `factor/core/include/gpu_cofactorization_cl.h` |
| `test/layer2/test_ocl_ecm.c` | `test/layer2/test_ocl_ecm.cpp` |

三份 C 代码原样搬过来就都能按 C++26 编过，只有内核测试里的缓冲区要改：
裸指针加手动 `free` 换成 `std::vector`，省掉 malloc 返回值的类型转换，也
不用再手工释放。

`WITH_OPENCL` 这个开关本身也照上游的做法接进 Makefile：与 `BATCH_CUDA`、
`CUDA_POLY` 互斥（两套后端定义同样的 `gpu_*` 符号），机器上没有 CUDA 工具链
时 `BATCH_CUDA=1` 自动回退到 OpenCL，两者都没有就只报个警。

内核源在目录重排时搬到了 `factor/core/` 下，驱动里写死的路径也跟着改了。

### SIQS 的参数表少了一行，410 位以上的输入因子基算小了六到七倍

`factor/siqs/siqs_aux.cpp` 里的 `NUM_PARAM_ROWS` 是 31，但 USE_AVX512F 那张参数表
（以及默认那张）只填了 30 行，末行是零初始化的 `{0,0,0,0,0,0,0}`。而查表的两个循环
拿 `param_table[i+1][0]` 当区间上界、"超出表尾就按最后两行的斜率外推"那段拿
`param_table[NUM_PARAM_ROWS-1]` 当有效行——都落到了那个零行上。

后果是位数超过 410 位（≈124 位十进制）时，两个循环都匹配不到区间（匹配要
`bits <= param_table[30][0]`，而它是 0），于是落到外推分支，用零行算出斜率
`(0-496628)/(0-410) = 1211`——正确的斜率是 `(496628-413857)/(410-400) = 8277`。
错的是外推用的斜率：零行算出来是 1211 每位，正确值是 8277 每位（用表里最后
两行 496628 与 413857 算），差 6.8 倍。因子基本身小多少取决于位数离表尾多远
——431 位时小 28%（670464 个素数对 523280 个），位数越大差得越多。另外
`large_mult` 被设成 0，表示完全不用大素数部分关系。

受影响的是 410 位（≈124 位十进制）以上的输入。410 位以内本来就走表里的参数，
与这个 bug 无关；那几个尺寸的二次筛慢，是因为它在那个尺寸上本来就慢 ——
yafu.ini 的 tune_info 里 QS/NFS 交叉点是 97.79 位，说明 100 位附近二次筛与
数域筛是同一量级。

改成先算出最后一行真正填了数据的下标，循环边界与外推都用它。实测：

| 输入 | 修复后因子基素数 | 素数上限 | 大素数倍数 |
| --- | --- | --- | --- |
| 100 位 | 120240 | 3353219 | 150 |
| 130 位 | 678752 | 21446237 | 175 |
| 150 位 | 1233312 | 40598461 | 105 |

修的是上游带来的问题（上游那张表也是 30 行）。

### 默认线程数从 1 改成全部核心

`options->threads` 与 `options->lathreads` 的默认值都是 1，所以 yafu 每次分解都
只用一个核，本机 6 核，SIQS、ECM、格点筛的并行部分全都没用上。实测同一批输入：

| 输入 | 1 线程 | 6 线程 |
| --- | --- | --- |
| 25 位 | 0.06 秒 | 0.05 秒 |
| 35 位 | 0.09 秒 | 0.09 秒 |
| 50 位 | 0.18 秒 | 0.13 秒 |
| 70 位 | 5.93 秒 | 1.35 秒 |

多线程从来不吃亏，小输入也没有线程开销的代价，所以默认值就用核心数。核心数的
探测放在 ytools（本来就管平台差异的那一层），用 `sysconf(_SC_NPROCESSORS_ONLN)`。
命令行 `-threads` 与 `yafu.ini` 里的 `threads=` 仍然优先：启动顺序是 ini 先、
命令行后，命令行覆盖 ini。

### yafu.ini 里有一行写错或太长，不该让 yafu 起不来

`readINI` 对 ini 里每一行都调 `applyOpt`，而 `applyOpt` 对两种情况是 fatal
的：选项名不认识、或者参数长度 ≥ `MAXARGLEN`(256)。那是命令行的处理方式 ——
命令行给错选项就该退出。

但 `yafu.ini` 是手改的配置文件，里面有几十行注释掉的设置等着取消注释。于是一
个拼错的键、或者漏了等号、或者某一行太长，yafu 启动时直接 `exit(1)`，什么也
干不了，报错还是 "invalid or out-of-range argument for option xxx"，看不出
是 ini 的哪一行有问题。

现在 `readINI` 在调 `applyOpt` 之前先自己挑一遍：不认识的键、或者参数过长的
行，都打印一行警告（带上文件名与键名）然后跳过。命令行那边不动。

顺带一提，选项自己那句 "argument to tune_info too long, ignoring" 以前永远走
不到 —— `applyOpt` 开头那条通用检查先退出了。

### 补上 restart_siqs 的除零防护

`filter.cpp` 里 `restart_siqs` 有
`pmax = sconf->large_prime_max / sconf->large_mult;`，而 `large_mult == 0`
是 SIQS 明确支持的状态（`SIQS.cpp` 那段专门处理它，当成 TLP 的 LPB）。这里
无条件相除，碰上就是除以零。

参数表缺行那版代码里，410 位以上的输入正好会被设成 `large_mult = 0`，于是
"从保存文件续算"走到 `restart_siqs` 就是 SIGFPE。触发条件已经没了，但防护
本身该有。`SIQS.cpp:4291` 那处除法在 `else` 分支里，已经被排除掉 0 与大于
1000 的情况，是安全的，不用动。

### C++26 落地：三条实际用起来的地方

树里早就是全 `.cpp` 加 `-std=c++26` 了，这一轮是把新特性真正用起来：

- **`std::expected` 用于错误传播。** `factor/mpqs/` 的线性代数原来在矩阵列
  溢出时直接 `exit(EXIT_FAILURE)`，整个进程当场消失；`solve_linear_system` 与
  `build_matrix` 现在返回 `std::expected<void, std::string>`，原因交回调用方，
  由 `factor_mpqs` 记一行日志后继续。`factor/core/tune.cpp` 的 `update_INI` 与
  `make_job_file` 同样改成返回 `bool`，失败时打印原因而不是把交互会话带走。
  方向与 `factor/nfs/gnfs/poly/` 里已有的 `poly_search_error` 一致。
- **`std::string_view` 用于命令行选项解析。** `top/cmdParser/cmdOptions.cpp`
  里 133 处 `strcmp(opt, OptionArray[i]) == 0` 变成 `opt == OptionArray[i]`，
  选项名与参数解析的辅助函数签名一起改成 `std::string_view`。
- **C++26 已经是编译标准**，`-std=c++26` 由 Makefile 统一带上。

### 清理与修正

- 删掉源码树里 121 个构建迁到 `build/` 之前留下的旧 `.o`（都在 `.gitignore`
  里，`make clean` 也会扫，但它们一直在那儿）。
- `build/factor/siqs/` 里 6 个源文件已删的孤儿 `.o`（make 不会自动清理）。
- `top/docfile.txt` 里的版本号从 `3.0-beta` 改成实际版本号——它一直是上游
  文档里的残留，和 `yafu.h` 的 `YAFU_VERSION_STRING` 对不上。

---

## 2.0.0

### 修掉 `-e` 段错误与 `-obase` 完全不可用

`top/cmdParser/cmdOptions.cpp` 的 `needsArg` 表和 `OptionArray` 按下标一一对应，
全靠位置对齐。两处标错：

| 选项 | 下标 | 原值 | 现值 | 后果 |
| --- | --- | --- | --- | --- |
| `e` | 67 | 0 | 1 | `yafu -e expr(6*7)` 段错误退出（SIGSEGV），表达式被丢弃 |
| `obase` | 109 | 0 | 1 | `yafu -obase 16 expr(255)` 报 `invalid or out-of-range argument`，该选项等于没法用 |

`-e` 的段错误链条：`needsArg == 0` 使它落进"不收参数"分支，`applyOpt("e", NULL)`
再调到 `applyArg(NULL, 0, ...)`，那里直接 `strlen(NULL)`。`processOpts` 里那句
`if (strcmp(optbuf, "e") == 0 && numOpt++ >= options->numArguments)` 写在
`needsArg == 1` 分支里，说明当初就是按"带参数"写的，只是表里填反了。

两处都改成 1 之后，`-e` 的表达式经 `applyArg` 存进 `inputExpr`，`-obase` 也能正常
收到它的进制数。`applyArg` 另加一道 `arg == NULL` 直接返回：参数可选的选项本来
就会以 NULL 调到这里，之前只是碰巧没有别的选项走到这里。

`needsArg` 是位置表、`NUMOPTIONS` 是 133，下列行内注释早已与实际下标错开，
所以在表上方写明"改哪一项先按下标核对 `OptionArray`"。

### 修掉筛法器在非 AVX-512 构建下链接不过

`kernels/include/siever-config.h` 无条件 `#define ASM_SCHEDTDSIEVE2` 并声明
`tdsieve_sched2buf`，`gnfs-lasieve4e.cpp` 的试除调度处按

```c
#if defined(ASM_SCHEDTDSIEVE2) && !defined(AVX512_TDSCHED)
    b0 = tdsieve_sched2buf(...);
#else   /* AVX-512 gather 版 / 标量 C 版，都是完整实现 */
#endif
```

来选路径。但 `tdsieve_sched2buf` 的实现在 `.asm -> .cpp` 迁移时随汇编一起没了，
全仓库只剩声明。结果是：定义了 `AVX512_TDSCHED` 的构建（yafu 本体经
`AVX512_ALL=1`）走 `#else`，链接得过；不传 AVX 开关的独立回归走前一条分支，
必然 `undefined reference`。`make test-standalone` 的 lasieve 一半因此从来没真正
链接成功过。开关、声明和那条死分支一并删除，剩下的两条路径都是完整实现。

### 测试：新的回归驱动与它自己的自检

上面的链接失败之所以能长期藏着不报，是因为没人替各套脚本的退出码负责：三个
shell 脚本各自 trap 清理临时目录，顶层 Makefile 一条一行地调用，谁没把状态传
出来整轮就是绿的。现在由 `test/run_all.sh` 统一编排——逐套记录、逐套打印、
最后汇总，任何一套非 0 整体就非 0，并把它逐套调用的目标接到 `make test-all`。

配套的 `test/harness_selftest.sh` 验证驱动自己可信：子套件报失败时驱动必须
非 0、失败退出码要原样传出、挑不出套件时不能静默通过。回归套件自己被测，
这一点以前没有。

三个 shell 脚本的 trap 改成先存状态、清 trap、再原样退出：

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

原先 `trap 'rm -rf -- "$task_dir"' EXIT HUP INT TERM` 把清理和信号处理写成
一条：收到 Ctrl-C 时 trap 跑完，脚本从被中断处**继续往下执行**并照常打印
"checks: N passed"。信号各有各的 trap 之后，脚本会立刻停下。

需要更正一句我先前给出的判断：我说过"以 `rm -rf` 收尾的 EXIT trap 会把显式
`exit 1` 变成 0"。在 WSL 这版 dash 上按五种写法逐一测过，退出码都原样传出，
这条不成立——当时的实验里 `printf ... "$(basename $f)" "$?"` 的命令替换先把
`$?` 覆盖成了 basename 的退出码。真正的缺陷是上面那一条信号处理，不是退出码
被覆盖；现在的写法对两者都稳妥。

`test/Makefile` 删除。它列的是 `.c` 源文件（早已迁成 `.cpp`），`YAFU_ROOT`
硬编码成上游作者的 `/sppdg/scratch/buhrow/...`，全仓库没有任何地方调用它，
真正的测试目标都在顶层 Makefile 里。留着只会让下一个人以为测试有两套入口。

### 收尾 C++ 化：删掉 52 个不参与构建的 .c

`git ls-files '*.c'` 列出 52 个 `.c`，但逐个核对三个 Makefile 的源文件列表后，
**参与构建的是 0 个**——构建的 225 个源文件已经全是 `.cpp`。这些 `.c` 是历次
合并与迁移后留下的旧版本：

| 目录 | 被谁取代 |
| --- | --- |
| `shared/arith/{arith0..3,limb1,limb2,mod64,mod128,mul_mod_64}.c` | `arith.cpp`（顶层 Makefile 里那段"如果你的树把 128 位代码拆成了 limb1.c/limb2.c"的注释就是这个时期的） |
| `shared/arith/tfm/*.c`（15 个） | `monty.cpp` |
| `siqs/` 的 64k 系列、knc/knl 系列、`sieve.c`、`poly_roots*.c`、`tinySIQS.c` | 对应的 `.cpp` ISA 内核 |
| `shared/common/{ocl_xface,lanczos/lanczos_reorder}.c`、`core/prime_sieve.c`、`nfs/winsupport.c`、`ecm/{avxppm1,vecarith52_karatsuba}.c` 等 | 各自的 `.cpp` 版本或已废弃的 OpenCL/GPU 路径 |

删除前按符号逐个验证过：对每个 `.c` 抽出它定义的非 static 函数，确认这些名字
**已由参与构建的文件提供**；其中 4 个文件另有一个构建里没有的符号，核对后
是三类无害情况——解析器把宏片段当成函数名（`M1`、`t52`、`_8`），以及
`tiny_process_poly`。

`tiny_process_poly` 是真问题：`qs_impl.h` 注释写着"实现在 tinySIQS.c 里"，
而那个文件不参与构建，于是这个函数（连同 `tiny_update_check`）**只有声明、
既无定义也无调用者**——谁写个同名函数就会在链接时才发现。随文件一并删除，
`qs_impl.h` 里两条声明也换成了说明。

顶层 Makefile 同步去 C：`objmap` 不再需要"同一份代码按 `.c`/`.cpp` 落到同名
对象"的兼容分支，三条 `%.c` 模式规则删除，头部注明这棵树只有 C++、全部按
C++26 编译。至此 `git ls-files '*.c'` 返回空。

删除走的是 `git rm`，1134 个提交的历史里随时可取回。

### 构建产物统一落在 build/

改动之前只有四个静态库待在 `build/` 里，其余生成文件都写在源码目录旁边：`top/*.o`、`factor/*/*.o`、`factor/mpqs/*.qo`、`factor/nfs/gnfs/*.no`、`factor/nfs/lasieve/objI11..16/`、`factor/nfs/lasieve/kernels/coreI11..16/`、依赖文件 `.deps/`，可执行文件 `yafu`、`yafu_test*` 也在仓库根。加上 `top/driver.o` 这种"改一行源码就得重编"的文件混在源码里，改名和查历史都很别扭。

现在所有生成文件都落在仓库根的 `build/` 下，目录结构镜像源码树：

| 产物 | 位置 |
| --- | --- |
| 对象 | `build/factor/ecm/ecm.o`、`build/top/driver.o`、`build/factor/mpqs/sieve.qo`、`build/factor/nfs/gnfs/gnfs.no` |
| lasieve 的 per-I 对象 | `build/factor/nfs/lasieve/objI<N>/*.o`、`build/factor/nfs/lasieve/kernels/coreI<N>/*.o` |
| 静态库 | `build/libysiqs.a`、`build/libyecm.a`、`build/libynfs.a`、`build/libmsieve.a`、`build/libyafu_common.a` |
| 依赖文件 | `build/.deps/factor/ecm/ecm.d` |
| 可执行文件 | `build/yafu`、`build/yafu_test`、`build/yafu_test_full`、`build/yafu_test_sanitize` |
| GPU 产物 | `build/*.ptx`、`build/sort_engine.so`、`build/collision_engine.so` |

做法上是三条：

- 主 Makefile 的 `objmap` 系列映射函数统一加 `build/` 前缀，编译规则从 `%.o` 改成 `build/%.o`，每个 recipe 自己 `mkdir -p` 输出目录与依赖文件目录。原来靠 `$(DEPS_SUBDIRS)` 这个 order-only 目录列表来建目录，但它在 `TEST_OBJS` 定义之前就展开了一次，`build/test/` 那层拿不到规则，所以不再维护目录清单。
- lasieve 子构建新增 `OBJDIR`（对象与库的输出目录，默认 `.`），顶层传绝对路径；`bobs` 打印的路径因此可以直接用作链接输入。`kernels/` 那层的委派规则从 `kernels/%` 改成 `$(KERNEL_DIR)/%`，委派时把 `OBJDIR` 一起传下去，且必须传完整目标 `$@` 而非 stem `$*`——后者会被源码目录里残留的同名旧文件满足掉，子构建静默跳过。
- lasieve 的 stat/test 目标（`mpqstest`、`ecmstat` 等）原本没有 `$(BINPREFIX)`，只能在源码树里构建再由 `test/test_lasieve.sh` 手工搬走。现在补上 `$(BINPREFIX)`，脚本改成统一传 `OBJDIR` 与 `BINDIR`，跑完源码树不再留任何生成文件。

顺带修掉两处：kernels 子构建原本靠 make 内置规则编 `%.o`，没有显式规则，`OBJDIR` 之下就没有规则可用，现已写出显式的 `.c`/`.cpp` 规则；`sort_engine.so`、`collision_engine.so` 和三个 `.ptx` 原来写在源码树里，源码按相对路径打开它们，所以 `stage1_sieve_gpu.cpp` 与 `gpu_cofactorization.cpp` 里的路径字面量也跟着加上了 `build/` 前缀（这几个路径是相对**运行目录**解析的，yafu 仍然要在仓库根运行）。

`make clean` 现在一次删掉整个 `build/`，并顺带扫掉旧落点里的残留文件。

---

## 1.1.0

### 二次筛的三套实现定名并全部接入计算器

三套二次筛此前只有 `siqs` 在计算器里可用，另两套一个只能被测试调用、一个只被编译进来。三个命令现在都能直接用：

| 命令 | 目录 | 出处 |
| --- | --- | --- |
| `siqs(n)` | `factor/siqs/` | 自初始化二次筛，S. Contini, *Factoring Integers with the Self-Initializing Quadratic Sieve* (1997) |
| `mpqs(n)` | `factor/mpqs/` | 多项式二次筛，R. D. Silverman, *Math. Comp.* 48 (1987) 329–339 |
| `pmpqs(n)` | `factor/pmpqs/` | 无文献名称，见下 |
| `tinysiqs(n)` | `factor/siqs/tinySIQS.c` | 只面向小输入的 SIQS 精简版 |

MPQS 筛的是 y(x) = (a·x + b)² − n，写成 b² − n = a·c；a 是平方时只需筛 (a·x² + 2b·x + c)。`pmpqs` 与它的差别只在 a 的取法：每个多项式从单个素数 d（要求 (n/d) = 1 且 d ≡ 3 mod 4）出发，Hensel 提升求出 b ≡ √n (mod d²)，于是 a = d²、c = (b² − n)/d²，即 a 是"一个素数的平方"，而通行实现里 a 是许多小素数之积的平方。这一变体文献里没有名字，`smallmpqs` 原来的命名又只比 `mpqs` 多一个前缀，读起来像"小号 MPQS"，故按其定义特征改名为 `pmpqs`。原命令名不再保留。

### 修掉 msieve 与 yafu 的头文件冲突

`mpqs.h` 会经 `ms_common.h` 拉进 msieve 的 `mp.h`，而 yafu 的 `core_types.h` 把同名 `mp_t` 定成 64 字长、msieve 定成 32 字长。同名不同尺寸，两者无法共处一个编译单元——这是 MPQS 此前无法从 yafu 侧调用的真正障碍。

- `factor/mpqs/include/mpqs_xface.h` 与 `mpqs_xface.c`：新增一层只讲 `mpz_t` 的门面，把 msieve 的头文件挡在边界之外。yafu 侧只通过它调用 MPQS。
- `util.h` 与 `ytools.h` 都定义了 `xmalloc`/`xcalloc`/`xrealloc` 和 `enum cpu_type`。这两组符号用 `YA_ALLOC_DECLARED` 与 `YA_CPU_TYPE_DECLARED` 两个宏做成"先到先得"，两边语义相同，谁先被包含谁提供，与包含顺序无关。
- `ms_common.h` 的头文件保护宏原本是 `_COMMON_H_`，与 yafu 的 `common.h` 完全同名，谁先包含就把另一个静默作废。改名为 `_MS_COMMON_H_`。

### 修掉 MPQS 失败时直接结束进程

`poly.c` 与 `sieve.c` 里有四处 `exit(-1)`：多项式选不出来、因子位数凑不整齐，以及两处倒数表越界。它们表示"这个输入 msieve 处理不了"，不是"程序不能继续"。挂在计算器上时这意味着 `mpqs(19 位数)` 会把整个 yafu 杀掉，交互会话随之消失。

`poly_init` 与 `do_sieving` 改为返回 `int`，失败时释放已分配的筛法数组再返回。`factor_mpqs` 不用改：它的四个输出指针都初始化为 NULL、只在成功时赋值，现有的空指针检查正好接住。现在这类输入会打印一行说明并让计算器继续。

`gf2.c` 里还有一处 `exit(EXIT_FAILURE)`，判的是矩阵列溢出，触发条件与上面几处不同，改它要连带改 `solve_linear_system` 的签名，留着没动。

## 1.0.0

建立自用基线：以当前上游 `bbuhrow/yafu` 的 `963dbe9`（合并 Gerbicz 碰撞多项式选择）为起点，完成下列整理。

### 架构

目录按分解方法重组。每个方法一个以方法命名的子目录，实现与该方法私有的头文件分开放在 `include/`：

- `factor/core/` 调度层，决定每个数走哪条分解路线
- `factor/trialdiv/` 试除、Fermat、Pollard rho、SQUFOF、Lehman
- `factor/siqs/` 自初始化二次筛
- `factor/ecm/` 椭圆曲线分解，标量与 AVX-512 两套实现合并到一处
- `factor/nfs/` 作业编排，`factor/nfs/gnfs/` 数域筛本体
- `factor/mpqs/` 多项式二次筛
- `factor/siqs/` 小输入的捷径
- `factor/nfs/lasieve/` 外部格点筛

被多个方法共用的代码各保留一份，收在 `factor/shared/`：`arith/`（52 位 limb 算术）、`ysieve/`（素数筛）、`common/`（线程池、线性代数、savefile）、`ytools/`、`cub/`、`aprcl/`（素性判定）。全部头文件在 `factor/shared/include/`；只被单个方法用到的头文件下沉到该方法的 `include/`。

静态库输出改到 `build/`，仓库根目录不再产生中间文件。

### 构建与依赖

必需的系统库只有三个：GMP、gmp-ecm、OpenMP。ECM 与 OpenMP 原先是可关闭的开关，现在是无条件依赖——缺 `ecm.h` 直接停止构建并说明该装什么。zlib 依赖消失：唯一用到它的 msieve demo 已删除，yafu 走的是 SIQS 自己那份 savefile 实现，链接结果里没有任何 zlib 符号。

GMP 路径改为按 multiarch 探测，不再写死某一发行版的路径。编译选项变化会自动触发重编，不再复用按旧选项编出来的目标文件。本机配置 `config.mk` 直接纳入版本管理，作为使用示例。

### 移除

不再需要因而删除的内容：第二份筛法副本（lasieve4_64、nfsathome-windows）、四个 demo 目录各自的 `calc.c`/`cmdOptions.c` 复制品、两份停用的 `.stash.c`、内置的 zlib 副本、旧的三份分类 Makefile、Windows 预编译二进制、MSVC 工程文件、已提交的 19.8 万行 GPU PTX、一个无人引用的空头文件。

### 修复

源码审查后修掉的缺陷类别：运算符优先级错误导致的行为异常、输出参数被覆盖、两处除零崩溃、缓冲区尺寸不足导致的截断、越界读写、未初始化内存被读取、无界重试循环、指向错误操作数槽位导致 `pp1`/`trial` 全部崩溃、128 位输入重建时移位位数错误、52 位 limb 进位掩码宽度不符、素数筛计数与实际筛除范围不一致、相对 include 依赖搜索路径巧合才能解析。

### 测试

- `make test-run` 分层回归（算术、素性、ECM、SIQS）
- `make test-cli` 命令行回归
- `make test-standalone` 独立编译的 NFS 与 lasieve 回归
