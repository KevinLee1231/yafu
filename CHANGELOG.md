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

## 3.0.1

这一轮是把源码从头到尾读了一遍，逐个模块找算法错误和工程瑕疵。修完的东西
分成三类：真的算错、真的越界、真的编不过。

### 命令行选项表整体错位，四十多个选项的参数语义全错

`top/cmdParser/cmdOptions.cpp` 里有三张靠下标对齐的表：选项名 `OptionArray`、
说明 `OptionHelp`、以及"这个选项要不要参数" `needsArg`。C++ 化那轮把
`ggnfs_dir`（指向外部格点筛可执行文件的目录，格点筛并进 yafu 之后就不需要了）
从 `OptionArray` 里删掉了，但另外两张表里对应的那一项忘了删，于是从第 30 项
起整体错开一位：后面 102 个选项的 `needsArg` 都指向了别的选项，其中 44 个因此
取到了错的取值。

后果分两种：

- 该收参数的被当成开关。`yafu -max_siqs 5 ...` 会走到
  `parse_int(NULL)`，打印 "invalid or out-of-range argument for option
  max_siqs" 然后直接退出进程。`-op`、`-o`、`-work`、`-lathreads`、
  `-nfs_params`、`-analysis`、`-nfs-stage1-args` 等同理。
- 不该收参数的反而把命令行里下一个参数吞掉。`-keep_afb 12345` 会把
  12345 当成 keep_afb 的值，真正的输入表达式就没了。

上一轮修过其中两个（`-e` 与 `-obase`），办法是手工改表里的值绕过错位，
并在表头写了注释提醒。现在改成把三张表对齐（`needsArg` 按选项名重新取值），
并加了一条防回归的检查：`test/layer3/test_options.cpp` 里的
`t_needsarg_consistent` 逐个选项在子进程里用空参数调一次 `applyOpt`，
表和分支对不上就会失败。

### Layer 0-3 的集成测试从来没被编译过

`make test-full`（含 SIQS 集成与计算器的那一套）一直是坏的，只是没人发现：

- 它用 `$(CC) $(CFLAGS)` 去编 `.cpp` 源文件（C 编译器 + C11 档位）。
- 新加的 `test/layer3/test_ecm_review.cpp` 里还有三处 C 风格的隐式指针转换，
  C++ 下直接编译失败。
- 链接时没有带上格点筛的目标文件，`lasieve_run` 十六个 undefined reference。

于是包含 SIQS 集成用例的那部分一直是不执行的，`make test-all` 里也没有
它。现在改用 `$(CXX) $(CXXFLAGS)`、补上 `$(LASIEVE_FILES)`、修掉那三处转换，
并把这一套作为 `full` 纳入 `make test-all`。它第一次真正跑起来就抓到两个
问题：默认线程数改成核心数之后 `test_options` 还在断言 1；以及上面那个选项
表错位。

### 真的算错的地方

- `factor/nfs/gnfs/poly/poly_skew.cpp` 的 `find_poly_core` 成功路径没有
  `return`。返回类型是 `std::expected<void, poly_search_error>`，不返回值是
  未定义行为：调用方读的是 `!search`，`has_value` 是垃圾值。
- `factor/ecm/microecm.cpp` 的二阶段曲线表：`B1` 不是受支持取值时
  `barray` / `numb` 整个没初始化，后面的循环拿栈上的垃圾值当上限，
  再拿 `barray` 越界读。
- `factor/trialdiv/trialdiv.cpp` 的 `spfermat`：算出一对近平方因子时直接
  `return 0`，把已经算出来的因子丢掉了。真正的因子是 `gcd(n, a + b)`，
  和主循环末尾用的是同一套算法。

### 越界

- `factor/shared/ysieve/count.cpp` 的孪生素数边界检查：判据拿块下标去比字节数
  `numlinebytes`，量纲差 8 倍，条件恒真，最后一个块会读到 `lines[0][numlinebytes]`。
  行缓冲是按 `numlinebytes` 精确分配的（`xmalloc_align` 在长度是 64 倍数时
  不留富余），所以这是实打实的越界读。

顺带澄清一处看着像越界、其实不是的：`applyOpt` 开头就有通用检查
`strlen(arg) >= MAXARGLEN` 就报错退出，所以传进各分支的参数最长 255 个字符，
`strcpy` 进 `char[MAXARGLEN]` 恰好放得下。分支里那些"太长就忽略"的检查
（`cuda_sieve`、`nfs_stage1_args` 等）因此都走不到，是上游的既有写法，
这轮没有动它们。

但表错位时 `nfs_stage1_args` 的 needsArg 是 0，"参数必填"那道检查就不触发，
NULL 传进 `strcpy` 直接段错误 —— 这条是表错位的后果，表对齐之后自然消失。

### 让 C++26 真正成立

- 320 处字符串字面量与 `PRIu64` 之类宏连写（`"..."PRIu64`）。C++11 起这是
  ill-formed，GCC 报了 217 次 `-Wliteral-suffix`；全部补上空格。
- 25 处格式串与实参类型不匹配（`%d` 配 `uint64_t`、`%u` 配 `size_t`…）。
- 对 `volatile` 计数器做 `++`：C++20 起废弃，C++26 直接删除。改成读-改-写。
- `#pragma ivdep` / `#pragma vector aligned` / `#pragma novector` 是 MSVC 的写法，
  GCC 每次编译都要抱怨。改成 `#pragma GCC ivdep` / `#pragma GCC novector`，
  MSVC 专用的那两条用 `#ifdef _MSC_VER` 圈起来。
- `always_inline` 少了 `inline`，GCC 会警告函数可能没法内联。
- `CXXFLAGS` 里留着 `-Wstrict-prototypes`（只对 C 有意义），每个 C++ 文件
  都要报一次 "valid for C/ObjC but not for C++"。

### 清理

- `new_poly.cpp` 的 `new_poly_a_test`：`break` 之后还有一段永远到不了的
  "丢弃重来"逻辑，以及一个只写不读的 `min_ratio`。
- 四处没人跳过去的死标签；`soe.cpp` 拼 CPU 特性串的 `sprintf`/`strncpy`
  换成有界的 `snprintf`。
- `test/run_all.sh` 报失败时打印的退出码一直是 0：`if` 语句没有 `else`
  分支时 `$?` 取到的是 `if` 本身而不是那条命令。改成在 `else` 分支里取。
- 计算器的 `expansion` 命令印"找到 N 对孪生素数 < 10^10"，但孪生那一步
  筛到的是 10^9 —— 10^10 是下一步筛素数的上界。改成印实际筛到的范围。

整树的编译告警从四千多条降到五百多条，剩下的都是转换类与未使用变量，
不涉及正确性。

### 进位/借位的约定反了，可移植分支算出来的和与进位是错的

这是这一轮最实的一个 bug。同一个宏名 `_addcarry_u64` / `_subborrow_u64`，在
不同文件里约定的返回值和出参含义是相反的：

    MSVC 的 _addcarry_u64(c_in, a, b, out)   返回进位，out 写和
    GCC 的 __builtin_addcll(a, b, c_in, out) 返回和，out 写进位

`arith.cpp`、`monty.h`、`microecm.cpp`、`micropm1.cpp` 四处各抄了一份
GCC 的 builtin 宏（源码里都留着 "these defines should be shared" 的 TODO），
而它们的调用点 —— spAdd/spAdd3/spSub/spSub3、uecm_submod、upm1_submod、
mulredc 系列 —— 全是按 MSVC 约定写的。x86-64 汇编分支和 lasieve 的内核走的是
原生内建，一直是对的，所以错只出现在可移植分支（非 x86-64，或
-DASM_ARITH_DEBUG）上：

  * spAdd/spAdd3/spSub/spSub3 的"和"与"进位"整个反了；
  * uecm_submod / upm1_submod 判的是"差是不是非零"而不是"有没有借位"，
    借位时把 0 或 1 当成差再加 n，结果完全不对。

这两处各配了一条回归：`test/layer0/test_mp_arith.cpp` 的 `sp_addsub` 按
"和/进位"的定义核对 spAdd/spSub/spAdd3/spSub3；`test/layer3/test_ecm_review.cpp`
的 `uecm_modarith` 按定义核对 (a-b) mod n 与 (a+b) mod n（为此把
uecm_submod / uecm_addmod 从 microecm.h 里导出来 —— 它们本来就是 inline 的
非 static 函数，只是没在头文件里声明）。旧写法在 10 个用例里错 5 个，
新写法全对。

现在把进位/借位从 `mp_platform.h` 里拆成独立的 `carry_intrin.h`（`mp_platform.h`
里还有 `_umul128` / `_udiv128` / `_reset_lsb` / `_lead_zcnt64` 那一组，和
`arith.h`、`arith.cpp` 自己定义的正面冲突，不能被那几个文件连带引入），
四份重复的宏删掉，统一用 `mp_addcarry_u64` / `mp_subborrow_u64` /
`mp_subborrow_u32` —— 约定一律是"返回进位/借位，out 写和/差"，实现按
"原生内建 → clang builtin → 可移植 C"分三档，跨平台都靠它。调用点只换名字，
形状不变。

顺带修掉可移植分支的 spAdd3：它把第一步的进位又塞进了第二步的进位输入，
等于加了两遍。`0xFFFFFFFFFFFFFFFF + 1 + 1` 的低位正确值是 1，代码算出 2。
x86-64 汇编分支一直是对的，把两条分支的结果放在一起比才看得出来。

`test/layer0/test_mp_arith.cpp` 里加了 `sp_addsub` 一条回归，直接按"和/进位"
的定义核对数值。

### 线程池的回调改成 C++26 的 std::copyable_function

`factor/shared/ytools/threadpool.h` 里的回调是"函数指针存成 void*、调用时再转
回来"：函数指针与 void* 之间的转换在标准里只是有条件支持，而且编译器查不出
签名写错 —— `ecm_start_fcn` / `ecm_stop_fcn` 实际上是 `void(tpool_t*)`，被
声明成 `void(void*)` 一直没人发现。

C++26 的 `std::copyable_function`（P2943，GCC 16.2 已有）替掉它：签名不对
直接编不过，带捕获的 lambda 也能注册。`tpool_t` 因此含非平凡成员，`malloc`/
`free` 不再合法，改成 `new[]`/`delete[]`，并把释放收进 `tpool_free()`，
免得各调用点自己写错。树里 24 处 `(void*)&func` 强转全部去掉。

### C++26 契约

新增 `factor/shared/include/yafu_contracts.h` 与违约处理函数
（`handle_contract_violation` 报出条件原文与源码位置后终止）。挑的三处都是
"违约即越界/静默算错"的地方，正好是前几轮精读实际抓到过的 bug：

  * `lanczos.cpp` 的 `shift_right_words`：位移超过字数时返回值
    `sz_a - wordshift` 会下溢成巨大的无符号数；
  * `siqs_aux.cpp` 的 `get_params`：参数表少填一行时外推斜率算成 1211/位
    （正确 8277/位），因子基小六到七倍 —— 现在断言表里最后两行是真数据；
  * `gnfs/gf2.cpp` 的 `build_matrix_core`：稠密行字宽超过 32 时
    `dense_rows[]` 越界写（`num_dense_rows` 在 1009..1024 时正好落在这个
    缺口里）。

契约检查的开销实测是 1.01 倍（GCC 生成一条冷路径分支），热路径上也吃得消。

### 第三方代码（lasieve / ECM 副本）里同样要修

`factor/nfs/lasieve/` 下的 `ecm.cpp` / `pm1.cpp` / `microecm.cpp` /
`micropm1.cpp` / `tinyecm.cpp` 是 `factor/ecm/` 那几份的改版副本（加了命名
空间包装与自己的 include），不是同一份文件 —— 修一处不会自动落到另一处。
这一轮在这两份上都找到了问题：

- 两份 `microecm.cpp` 的 `uecm_stage2`：stg1_max 不在预置表里时
  `barray` / `numb` 整个没初始化，后面的循环拿栈上的垃圾值当上限，再拿
  barray 越界读。
- 两份 `tinyecm.cpp` 的 `prac_good`：`v = val[i]` 里的 i 只在下面这个循环
  里被赋值，而循环在退化情况下一次都不会命中 ——
  `lucas_cost` 返回的正好是 `ADD * n`，而初值 `cmin` 就是 `ADD * c`，
  两者相等，`cost < cmin` 不成立。于是 i 是栈上的垃圾值，val 只有 NV(10)
  个元素，`val[i]` 越界读。c == 1 时就是这个退化情况（r = round(0.618) = 1
  >= n = 1）。改成 `int i = 0`，取 val[0]（黄金比，注释里写的默认值）。

### resieve 的越界读

`factor/siqs/tdiv_resieve_32k_avx2.cpp`：resieve 按块处理素数（8/16/32 个
一起），循环条件只看块的起点，所以最后一块可能越过 med_B。越界位置上的素数
没初始化过，比较结果是垃圾，可能凑出假命中。

- AVX512 路径是"先读 `fbc->prime[buffer[i]]` 再说值不对" —— 读本身就越界
  （`comp_sieve_p->prime` 只按 med_B 个元素分配）。
- AVX512 路径一旦遇到假下标就 `break`，后面所有的除法全放弃了；AVX2 那条
  路径用的是 `continue`。两条路径行为不一致。

改成：先验下标在界内，再取素数；AVX512 路径也改成 `continue`。

### 剩下的 TODO 标记逐个看过

全树 28 处 TODO/FIXME/XXX，处理完剩 11 处：

- 过期的、描述与代码不符的，删掉（`monty.cpp` 与 `lasieve/tinyecm.cpp` 里
  "implement portable u128 x u128 modular multiplication" —— 实现就在下面；
  `limb1.h` 与 `monty.h` 里 "need something portable" —— 可移植实现也在下面；
  `lasieve/mpqs3.cpp` 里 "replace 1024" —— 早就是命名常量了）。
- 确实还没做的事，保留，但把"要做什么、为什么、代价在哪"写清楚，别让下一个人
  再猜一遍。
- `lasieve/gnfs-lasieve4e.cpp` 里那个 `Schlendrian("XXX\n")` 换成能定位的
  报错（原来真触发了也只知道一个 XXX）。

另外 `factor/shared/arith/fftmul.cpp` 有三处"假定 mp_limb_t 是 64 位"，
现在改成编译期的 `static_assert`：32 位 GMP 的 limb 是 32 位，算法会算错，
不如在编译期就拦下来。

`factor/shared/aprcl/tinyprp.cpp` 的计时器也整理了：原来 Apple 分支里三句
`return` 连着写，只有第一句（`val * 32`）能执行，后面两句是死的，而那句
`* 32` 本身就是个猜测。现在 aarch64 直接用 `cntvct_el0` 的计数值，其它平台
用 `steady_clock` 的纳秒计 —— 顺带去掉了那句会让非 x86-64、非 aarch64
平台根本编不过这个文件的 `#error "unsupported _rdtsc implementation"`。

### lasieve 里的 C++26 已经删除的写法

`factor/nfs/lasieve/gnfs-lasieve4e.cpp`：

- 对 volatile 计数器做 `++`（两处），以及把 volatile 赋值的值当条件用
  （一处）：C++20 起废弃，C++26 直接删除。改成读-改-写 / 拆成两条语句。
- `case 'n'` 落到 `case 'N'` 是有意的，加上 `[[fallthrough]]` 把意图写明。
- `fprintf(g_ofile, "")` 是句空操作，删掉。

连同整树一起，把 6 处字符串字面量与格式宏连写（lasieve 用的是自己的
`UL_FMTSTR`）补上了空格 —— C++11 起 `"..."MACRO` 是 ill-formed 的。

### 第三方副本里还有两处同类缺陷，以及构建系统的一个洞

* 两份 `tinyecm.cpp`（`factor/ecm/` 与 `factor/nfs/lasieve/`）的
  `ecm_stage2` / `ecm_stage2_x8` 里，`stg1_max` 不在预置表里时
  `barray` / `numb` 整个没初始化，后面的循环拿栈上的垃圾值当上限，再拿
  barray 越界读。和 microecm 那份同一个毛病。
* `factor/nfs/lasieve/` 子工程（含它的 `kernels/`）的编译规则**只依赖 .cpp，
  不依赖任何头文件**。改了 `include/if.h` 之类的头不会重编，于是陈旧的目标
  文件与新的声明对不上 —— 链接时报 undefined reference（mangled 名字带参数
  类型，签名一改就找不到定义了）。这次改 `complain` 等函数的签名时就撞上了。
  现在把两个头文件目录都算进依赖。
* `complain` / `Schlendrian` / `logbook` / `errprintf` 的格式串参数声明成
  `char*`，而 C++ 里把字符串常量传给 `char*` 是被禁止的（`-Wwrite-strings`），
  整棵 lasieve 有两百多条这样的告警。这四个函数只读格式串，改成
  `const char*` 既是正确的签名，也把这批告警一次清掉。

顺带收敛了三处重复的宏定义：`factor/shared/include/util.h` 里 `INLINE` 被
无条件定义了两次（先是 `inline`，紧接着是空定义 —— 后一条总是赢，第一条是
死代码，条件在改写过程中丢了），`factor/shared/ytools/include/ytools.h` 里
还有第三份，于是两个头都进来时 GCC 报 "INLINE redefined"。`INLINE` 用在 109
处，全是头文件里的 `static INLINE 函数定义`，三种写法语义一样，取 `inline`
即可。`_POSIX_C_SOURCE` 同理，加了 `#ifndef` 守卫。

`factor/mpqs/poly.cpp` 里两处 switch 是有意的贯穿，加上 `[[fallthrough]]`
把意图写明。
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
