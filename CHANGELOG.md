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

### APR-CL 查表失败时会静默算出错误答案

`factor/shared/aprcl/mpz_aprcl.cpp`（以及 msieve 那份 32 位的
`mpz_aprcl32.cpp`）里的 `JacobiSum` 是这样找 J(p,q) 的：

```c
for(a = 0; a < JPQSMAX; a++)
    if(jpqs[a].p == myP && jpqs[a].q == Q)
      break;
```

表里没有这一项时，`a` 停在哨兵项上，而哨兵项的 `index` 是 0，于是
`sls[jpqs[a].index + I]` 读到的是别的 q 的 J 值 —— 算出一个错的答案，
而且全程不报错。现在查不到就报出缺的是哪一项并停下。

顺便把这份表本身核了一遍：`sls` 与 `jpqs` 两张表的长度、每条记录占的字数
（`PL = (P-1)·P^(K-1)`，K 是 p 在 q-1 里的幂次）、实际会发生的每一次查表，
三者完全闭合：3493 次查询正好对应 3493 条记录，没有缺失、没有越界、没有
用不到的冗余条目。`aiJS` 之类工作数组的下标上界（`PK = P·PM`，最大 32）
也正好等于 `PWmax`。

### 同一段数学的两套实现给出了不同的下标

`factor/shared/aprcl/tinyprp.cpp` 的 `lucas_prp_128x1` 与
`factor/shared/aprcl/mpz_aprcl.cpp` 的 `mpz_lucas_prp` 算的是同一个东西
（Lucas 序列的 U_(n-(D/n))），但在下标上不一致：`(D/n) == 0`（D 与 n 不互素）
时，前者无条件把下标减 1，后者保持下标为 n。同一个数在两套实现里会得到
不同的下标，进而得到不同的结论。现在按 `mpz_lucas_prp` 的做法对齐。

这个函数还把 p 写死成 1（`vh` 的初值就是 1，`vl` 里的 `p*ql` 也直接简化成
`ql`），却接受任意 p。现在签名里去掉了 p，只保留 q，需要别的 p 请走
`mpz_lucas_prp`。

### 线性代数读矩阵时写穿栈上的缓冲区

`factor/shared/common/lanczos/lanczos_io.cpp` 的 `read_matrix` 把一列读进
`uint32 tmp_col[MAX_COL_IDEALS]`（1000 个字）。原始 msieve 那里有一道
`num + dense_row_words > MAX_COL_IDEALS` 的判据，改写成 C++ 时这半句丢了，
只剩下"缓存里还剩多少字"那半句。于是某一列自称有 1000 个条目时，
拷贝长度是 1032 个字，栈上越界写 128 字节。同一个文件的注释里还引用着
这道已经不在的判据。现在把它补回去。

### MPQS 里三处"读到了没写过的内存"

* `factor/mpqs/mpqs.cpp` 构造因子基时，`modsqrt_array` 用 `xmalloc` 分配
  （不清零）。当素数 p 整除 n 时，那个格子的模平方根不会被赋值 —— 而正确
  的值是确定的 0（`p | n` 时两个根重合）。筛法拿这个未初始化的值去算根，
  会造出并不整除的"关系"，把后面的真关系一起拖掉。现在该格显式写 0，
  分配也换成 `xcalloc`。
* `factor/mpqs/relation.cpp` 的 `get_table_entry` 被三处**只读**调用点
  当作"查表"用，靠传 `new_entry_offset = 0` 表示"查不到"。但它照样执行
  miss 分支：把 `table[0]`（`cycle_table_size = 1` 预留的哨兵槽）当成新
  顶点覆盖，还让 `hashtable[first_offset] = 0` 把整条桶链从查找结构里
  抹掉。SIQS 那边（`factor/siqs/filter.cpp`）早就为此拆出了一个只读的
  `lookup_table_entry`，MPQS 这三处照着改。
* `factor/shared/common/filter/singleton.cpp` 读 `.lp` 文件时有三处
  `fread` 没检查返回值。文件被截断时 `ideal_count` 会留着上一轮的值
  （首次是栈上的未初始化值），而它是 `uint8`，最大 255 —— 拿它当长度会
  写穿只有 100 个字的 `ideal_list`。同一文件里的两趟版本都检查了返回值。
  现在这三处都检查，并把计数夹到缓冲区大小以内。

### lasieve 副本里的大素数：截断、无上界、丢一个、预留不足

`factor/nfs/lasieve/batch_factor.cpp` 与 `factor/core/batch_factor.cpp` 是
同一份代码的两个副本。主线那份上一轮已经修过大素数个数无上界的问题，
lasieve 这份副本没有跟上，于是四个问题叠在一起：

* **截断**：大素数存进 `uint32_t`，而 `lpb`（大素数上界）可以到 40 位
  （`process_batch.cpp` 明确允许 `lpb <= 40`）。33~40 位的大素数被截成
  32 位，写进关系的是一个并不整除范数的数。现在局部数组与 `cofactor_t`
  的字段都改成 64 位。
* **无上界**：`lp_r[num_r++] = ...` 从来没有上界检查，而数组只有
  `MAX_LARGE_PRIMES`（4）个元素。f1r 与 f2r 的贡献可以叠加，一条路径上
  最多能塞进 8 个以上的大素数，写到第 5 个就踩进了紧邻的 `lp_a[]`。
  现在改用主线那份的 `LP_STORE` 宏：装不下就记下溢出，这条关系作废。
* **丢一个**：`process_batch.cpp` 输出关系时只写 3 个大素数槽，而存储侧
  有 4 个 —— 一侧有 4 个大素数时，第 4 个被静默丢弃，关系与范数对不上。
  现在输出遍历全部 4 个槽（值为 1 的槽会被跳过）。
* **预留不足**：`relation_batch_add` 给因子表预留的字数写死成
  `2 * MAX_LARGE_PRIMES`（8 个字），而实际写进去的是两侧未因式分解余因子的
  32 位字数，与 `MAX_LARGE_PRIMES` 无关。余因子超过 256 位时就会写穿因子表。
  现在按真实需要的字数预留（两份副本同改）。

### 错误分支上的内存没放掉

* `factor/mpqs/sieve.cpp` 的 `do_sieving`：`poly_init` 失败时只放掉了
  `sieve_array`，此前分配的 `packed_fb` 和 `buckets`（连同每个桶 1000 项的
  列表）全泄漏。而 `factor_mpqs` 并不检查 `do_sieving` 的返回值，错误被
  吞掉，进程继续跑。
* `factor/mpqs/gf2.cpp` 的 `build_matrix`：某一列超出范围时直接返回错误，
  而前 i 列的 `col->data` 已经分配了。

### `-d` 配 gzip 输出会段错误

lasieve 的 `-d`（批量因式分解）模式会额外写一份未压缩的原始关系。输出走
gzip 管道时（`-o out.gz` 或默认的 `.gz` 名），走的是 `popen` 分支，
那份 raw 文件从来没有被打开过，`g_ofile_raw` 一直是 NULL —— 而写 raw 关系
时直接拿它当 `FILE*` 用，关系里只要有大素数就会段错误。

现在管道分支也把 raw 文件开起来，名字取同一份输出的未压缩形式
（`out.gz` → `out.raw`）。输出到 stdout（`-o -`）时没有地方放这个文件，
直接报错说明。

顺带修掉另外三处越界：

- `smallsieve_auxbound[2][5]` 只有 5 个槽，填边界指针的循环在 k == 0 时
  算出下标 5，写到了数组外面（s == 0 时踩到另一侧的上界指针）。
- `logbook` 格式化失败后没有返回，接着拿未初始化的 `output_str` 去
  `strlen`；格式化结果是空串时读 `output_str[-1]`，读到 malloc 块前面
  那个字节。
- `root_finder` / `root_finder64` 在 p == 2 的那一支不看多项式次数，
  最坏要写 3 个根，而根缓冲区只有 `poldeg` 个元素。现在按次数裁剪，
  缓冲区也留出了位置。

顺带把两条 TODO 标注改准确：批量因式分解模式早就接到 `-d` 选项上了，
usage 里一直写着"output raw file with cofactors > 64 bits for batch
factorization"；ECM 的 Suyama 参数化那条要排除的特殊值是 d == -1/8，
而 d = sigma^2 / 2^52，判断要写成 8*d + 1 == 0 (mod n)，不能拿 d 去
比 -1/8 —— 撞上的概率极低，而且撞上之后是跳过这条曲线还是直接报错
退出还没定下来，所以那条仍然留着。

### 续算时用的块大小没有落进文件，续算会用新的设置去解释旧的区间

`factor/nfs/nfs_sieving.cpp` 里那条"续算时用到的参数调整没有写进文件"的
标注，落到实处是一处会算错的东西：筛法块大小 `job->qrange` 根本没进过
任何文件。

已筛区间（`.ranges`）是按上次的块大小划的，续算时读回来的是每一段的
实际长度；而 `job->qrange` 来自本次 `get_ggnfs_params()` 的查表（或命令行
的 `-rangeq`），`sort_completed_ranges()` 直接拿它当"这一段多长"用。两边
对不上就会重复筛掉一段，或者跳过一段。

现在把 `qrange` 写进 `.job` 文件（yafu 自己写自己读的关键字，sieve 那边
不认它，不影响它读同一个文件），续算时用它覆盖查表值 —— 续算必须沿用
上次的设置，这与文件里其它参数（lpbr/alim/...）的既有约定一致。读的时候
取文件里最后一个 `qrange:`（`.job` 是追加写的，后面覆盖前面，与
`parse_job_file` 读其它关键字的约定一样），而追加的那一行始终是同一个值，
所以反复续算不会把参数改掉。

`test/layer3/test_qs_review.cpp` 加了一条回归，覆盖写入与读回。

### SIQS 的最后一个 B 值一直没有被赋值

`factor/siqs/filter.cpp` 的 `generate_bpolys()`：

```c
for ( ; numB < maxB; numB++)
{
    mpz_set(sconf->curr_b[numB - 1], dconf->curr_poly->mpz_poly_b);
    dconf->numB = numB;
    nextB(dconf, sconf, 0);
}
```

B 值一共有 `1<<(s-1)` 个（Gray 码的总数），循环填的是 `curr_b[0..maxB-2]`，
**最后一个槽 `curr_b[maxB-1]` 从未被赋值**。而数据文件里的 B-poly 索引
可以合法地等于 `maxB-1`（边界检查只挡 `this_id >= bpoly_alloc`），那条
关系读到的是 malloc 出来的未初始化内存，算出来的 Q 是垃圾。

现在循环结束后把最后一步的结果补存进去。`test/layer3/test_qs_review.cpp`
加了一条回归，检查整条 B 值序列 —— 相邻两步的差必须是 `±Bl[ctz(i)]`。

### B 值改成按需生成

同一个 A 多项式有 `1<<(s-1)` 个 B 值，但实际有关系引用的只有几个
（一个 A 产生的关系数通常是个位数）。原来在 `process_poly_a()` 里把全部
都算一遍，每个一次大整数拷贝加一次加减 —— A 多项式多的时候这笔开销很大。

同一个 A 的关系在数据文件里是连续的（读到新的 A 才会调
`process_poly_a`），B 索引也就递增，所以按需顺序生成即可，不需要随机
跳转。`process_poly_a()` 返回前会释放掉 `dconf` 的 gray/nu/Bl 并清掉
poly 的 a/b —— 那是"一次性生成全部"所依赖的做法；现在把这些状态转移给
`sconf`，由下一个 A 接手时再释放。

回归里加了一条对比：按需生成（乱序访问）与一次性生成必须逐值一致。

### APR-CL 查表失败不再静默算错，Lucas 自测的下标与 mpz 版对齐

`factor/shared/aprcl/mpz_aprcl.cpp`（以及 msieve 那份 32 位的
`mpz_aprcl32.cpp`）里的 `JacobiSum` 是这样找 J(p,q) 的：

```c
for(a = 0; a < JPQSMAX; a++)
    if(jpqs[a].p == myP && jpqs[a].q == Q)
      break;
```

表里没有这一项时，`a` 停在哨兵项上，而哨兵项的 `index` 是 0，于是
`sls[jpqs[a].index + I]` 读到的是别的 q 的 J 值 —— 算出一个错的答案，
而且全程不报错。现在查不到就报出缺的是哪一项并停下。

顺便把这份表本身核了一遍：`sls` 与 `jpqs` 两张表的长度、每条记录占的字数
（`PL = (P-1)·P^(K-1)`，K 是 p 在 q-1 里的幂次）、实际会发生的每一次查表，
三者完全闭合：3493 次查询正好对应 3493 条记录，没有缺失、没有越界、没有
用不到的冗余条目。`aiJS` 之类工作数组的下标上界（`PK = P·PM`，最大 32）
也正好等于 `PWmax`。

`factor/shared/aprcl/tinyprp.cpp` 的 `lucas_prp_128x1` 与
`factor/shared/aprcl/mpz_aprcl.cpp` 的 `mpz_lucas_prp` 算的是同一个东西
（Lucas 序列的 U_(n-(D/n))），但在下标上不一致：`(D/n) == 0`（D 与 n 不互素）
时，前者无条件把下标减 1，后者保持下标为 n。同一个数在两套实现里会得到
不同的下标，进而得到不同的结论。现在按 `mpz_lucas_prp` 的做法对齐。

这个函数还把 p 写死成 1（`vh` 的初值就是 1，`vl` 里的 `p*ql` 也直接简化成
`ql`），却接受任意 p。现在签名里去掉了 p，只保留 q，需要别的 p 请走
`mpz_lucas_prp`。它全树没有调用点，连同定义一起删掉了。

### 随机对拍：tinyprp 的标量与 AVX512 路径对 GMP 参考实现

`test/layer1/test_tinyprp_review.cpp` 新增四个测试：

* `random_64bit_mr`：2000 个随机 64 位奇数，`MR_2sprp_64x1` 对 GMP。
* `random_128bit_prp`：1000 个随机 128 位奇数，Fermat / Miller-Rabin /
  Selfridge / BPSW 四种对 GMP。
* `random_128bit_lucas`：1000 个随机 128 位奇数配上 Selfridge 的 d 序列，
  Lucas 自测对 `mpz_lucas_prp`。
* `random_104bit_avx512`：400 组 × 8 路的 104 位数，AVX512 的
  `MR_2sprp_104x8` 对标量的 `MR_2sprp_128x1`。

### CADO 的输出重定向不再走 shell 语法

`factor/nfs/nfs.cpp` 里调用 `convert_poly` 用的是
`system("... < in.poly > nfs.fb")`。`<` 输入重定向 cmd.exe 不支持，路径里
带空格时 sh 与 cmd 的引号规则也不同。

现在重定向在进程里做（新增 `ytools_run_redirected()`，用 `dup2` 换文件
描述符），命令里只留可执行文件路径与参数。本项目只面向 Linux，所以这个
辅助函数只有 POSIX 一支。

### mpqs3 的平方根逆元不再每次分解重算

`factor/nfs/lasieve/mpqs3.cpp` 的 `mpqs3_SI_init()` 里，每一项都要算一次
`s^{-1} mod p`（s 是该素数的平方根）。这两者都是因子基的常量 —— 因子基在
一次分解内不变 —— 所以现在在 `mpqs3_generate_FB()` 里一次算好存进
`mpqs3_FB_sq_inv[]`，`SI_init` 直接取。

同一处还有一句"逐项做模逆，整批一起算更快"：那一处不成立。每一项的模数
p（`mpqs3_Adiv[i]`）互不相同，而"一次求逆推出多个"的批量法要求所有元素
在同一个模数下。同一文件里 `SI_inv_table` 那种批量写法适用于"同一个 p 下
的多个元素"，不能套到这里。注释改成了这个说法。

### 计划文档里的待决问题逐条对过代码

`factor/nfs/gnfs/poly/` 下的三份计划文档（`dynamic-stage2-balancing-plan.md`、
`shared-threadpool-balancing-plan.md`、`shared-threadpool-balancing-plan_v2.md`）
文末各有一节 "TODO / open items"，共 23 条。逐条对着代码核过之后，每条
标了状态（已落地 / 选型变了 / 确实待定 / 功能不存在）并写清代码里的实际
做法与证据位置。

顺带发现：分块（chunking）的那套实现目前在树里是被 `if (0)` 关着的
（`factor/nfs/gnfs/poly/stage1/stage1.cpp:740` 那一整块），运行时走的是
`:802-813` 的整窗一次性调用，真正生效的 deadline 强制在引擎内层循环里
（`stage1_sieve_cpu.cpp:1349-1365`）。所以以"分块"为前提的几条，分块尺寸
那两个常量目前不影响运行。

文档三里"给每个生产者线程加一个 `stage2_local` 字段"那条标成了 resolved，
但代码里根本没有这个字段 —— CPU 路径复用的是共享的
`d->stage2_workers[threadid].sizeopt_data`，靠把数组撑到不小于线程数来
保证下标不越界。正文也一并改了。

### NFS 的平方根分解：没有合数剩余时才停

`factor/nfs/gnfs/sqrt/sqrt.cpp` 的 `nfs_find_factors()` 在找到一个因子之后，
判据是"剩下的合数不到 85 位（`SMALL_COMPOSITE_CUTOFF_BITS`）就停"。

85 位以内完全可能是两个 42 位素数的乘积 —— 那种余数交给 yafu 的 rho 要
2^21 次迭代才拆得开，而 NFS 用后面的依赖只要几秒。原来那句注释自己也写了
"其实应该停得更早（70-80 位）"，但为了不让 MPQS 跑掉已收集的 NFS 关系
才保持在 85 位。

现在判据改成"因子表里没有合数了"（`composite_bits == 0`）才停 —— 也就是
整个输入被完全分解为止。两因子的情况完全不受影响：找到唯一那个因子之后
剩下的都是素数，`composite_bits` 本来就是 0，行为一字未变。

依赖用完时循环自然结束，剩下的合数仍然由 yafu 的 rho 接手，不会分解不掉。

实测下来，yafu 的实际使用里 NFS 的这一段很少被走到 —— 三个素因子的输入
（75 / 104 / 134 / 164 位各造了一个）分别被 rho、SIQS、SIQS+ECM 在
0.03~0.2 秒内就分解完了，根本没进到 NFS。NFS 的多因子场景要等到因子
大到 rho 与 ECM 都啃不动、而总位数又还没大到 SIQS 划算的那个窗口。

### stage 2 配对那处的"接着上一段继续"为什么没有可实现的形态

`factor/ecm/avxecm.cpp` 里那条"让素数区间与配对生成器对上，新的一段能接着
上一段继续"的标注，按现在的划分是没有可实现的形态的，原因在两边的粒度：

* 段是按素数个数切的，一次 `PRIME_RANGE = 1e8` 个素数
  （`avx_ecm_main.cpp:594`）。B2 通常是 1e4~1e5 量级，所以段循环
  （`avxecm.cpp:1970` 的 `for`）通常只跑一轮 —— 根本没有"新的一段"。
* 就算 B2 大到有好多段，一段覆盖的 a 跨度是 `(B2-B1)/(2*w)`，而 Pa 表
  覆盖的 a 跨度只有 `2*L = 4*U`（U 的候选是 1..16，
  `avx_ecm_main.cpp:1145`）。两者差好几个数量级，新的一段的 Pa 与上一段
  没有交集。

要让"新的一段接着上一段继续"成立，段的划分就得改成按 a 范围、且每段的
跨度小于 `2*L`。那样段数会多出好几个数量级，而每段都要重跑一次 `pair()`
（Q 队列的进出），净收益要实测才知道。注释改成了上面这段话，把数字都摆
出来。

### mpqs3 的参数表：各列的含义与影响范围

`factor/nfs/lasieve/kernels/include/mpqs-config.h` 的 `mpqs3_param[15][7]`，
原先只有一句"optimize parameters"。现在写清：

* 15 行按分解复杂度分档（`mpqs3_choose_parameter()` 把 complexity 夹到
  `[91,148]` 再按 4 一档映射到行号，`mpqs3.cpp:739-747`）。
* 7 列依次是：因子基素数上限、筛法起始下标、每个多项式用的 A 因子个数、
  A 因子总池大小、筛选的对数界、试除起始下标、筛长度（读入处
  `mpqs3.cpp:754-765`）。
* 这张表只被 mpqs3 自己读。lasieve 主流程里大于 96 位的余因子走的是旧的
  `mpqs_factor` —— `mpqs3_factor()` 那一句被 `#if 0` 关着
  （`factor/nfs/lasieve/gnfs-lasieve4e.cpp:6505-6511`），所以这里的取值
  目前只影响 `mpqs3test` / `mpqs3stat` 这两个独立目标。要调参得先决定
  mpqs3 是否启用；启用之后用这两个目标测。

### ECM：删掉两段死代码，其中藏着确定的堆越界

这一轮做基准时撞到一次 `realloc(): invalid next size`（ECM 预测试阶段）。
追下去发现 `factor/ecm/avxecm.cpp` 里有两段从头到尾没有调用者的代码，
其中一段有确定的堆越界。

**`euclid()`（248 行）** —— 删除前定义在 `avxecm.cpp:1364`，前置声明在 `:69`，
全树无调用点；`git log -S "euclid("` 显示它从引入起就没有过调用者。
同一文件里的邻居 `next_pt_vec` 有三处调用（`:3032`、`:3074`、`:3081`），
是活代码，没动。

**`getEseq()`（95 行）** —— 定义在 `:700`，唯一调用点是 `euclid` 内部
（`:1420`）。它自己有一个确定的越界：

```c
int seqLen = 64;
int *seq = (int *)malloc(seqLen * sizeof(int));
int it = 1;
while (d > 0) {
    ... seq[it] = 1/2/3;     // 写 seq[it]
    it++;
    if (it > seqLen) {        // 只在 it > seqLen 时扩容
        seqLen *= 2;
        seq = (int *)realloc(seq, seqLen * sizeof(int));
    }
}
```

写 `seq[it]` 时 `it` 的合法上界是 `seqLen-1`，而扩容条件写成 `it > seqLen`，
漏掉了 `it == seqLen` 那一档 —— 那一轮循环体已经写过 `seq[seqLen]`，越界
一格（4 字节）。单独写了个程序复刻这段逻辑并加边界检查，确认它会触发：
`d=2, e=121` 这样的小值就在第 64 次迭代写到 `seq[64]`。

修法有两种：把 `it > seqLen` 改成 `it >= seqLen`，或者直接删掉整段死代码。
选后者 —— 越界虽然确定，但 `euclid` / `getEseq` 都不在运行路径上，
留着一段没人调、还带越界的代码没有价值。两段一起删掉，共 343 行。
删完 `avxecm.cpp` 的编译警告从 89 条降到 83 条，没有新增。

**关于那次 `realloc()` 崩溃**：不在上面这段代码里。`euclid` / `getEseq`
根本不执行（ECM 预测试找因子时走的是 `ecm_factor()`，即 gmp-ecm 库）。
yafu 传给 gmp-ecm 的参数是干净的：`thread_data->params` 每线程一份
（`ecm.cpp:848` 的 `ecm_init`），`thread_data->gmp_n` 每线程一份
（`ecm.cpp:852`），符合 gmp-ecm 对多线程的要求。B1=1000000 >
`ecm_ext_xover`（48000，有 AVX512 时）所以走的是 gmp-ecm 7.0.6 库内部。
连跑 12 次同样的输入零复现（12 次全部 rc=1 正常退出），gdb 下也没有
命中 —— 是一次极罕见的第三方库内部故障，yafu 侧无法复现、也无从修改。

### 顺带查清：stage1 的 `num_sort` 预留量

基准时用 `p[1000,200000]` 这种远超真实比例的 p 范围，触发了
`stage1_sieve_cpu.cpp:1085` 的段错误。查下来是上游 msieve 原有的限制
（`1ca7621` 引入时就是 `num_sort + 100`）：单个 root 能贡献的条目数是
`sieve_size / pp ≈ (p_max/p_min)^2`，不是常数 100。

实测边界（`(p_max/p_min)^2`）：5.76 / 25 / 100 都正常，400 触发堆损坏，
2500 触发段错误。而真实调用里 `p_min = p_max / P_SCALE`，`P_SCALE = 2.4`，
比值恒为 5.76 —— 离触发边界很远，真实路径不会踩到。

### stage 1 的分块：deadline 语义有个真 bug，已修

上一节说"分块唯一买到的东西，整窗调用已经做到了"，这话说得不完整 ——
**分块那套实现本身是坏的**，坏在 `coeff_deadline` 的语义被悄悄放大了。

**bug 在哪。** 三个引擎的 deadline 判据都以"**本次 specialq 调用开始**"
为起点：

- cpu_gerbicz：`stage1_sieve_cpu.cpp:1211` 取 `cpu_start_time`，
  `:1361` 判 `(get_wall_time() - cpu_start_time) > task->coeff_deadline`
- hashtable：`stage1_sieve_cpu_hashtable.cpp:933` 判
  `msieve_difftime(&tstart, &tstop) > deadline`
- GPU：`stage1_sieve_gpu.cpp:1104` 判 `elapsed > deadline`

而 `coeff_deadline` 的本意是"**这个 a_d 的总预算**"
（`nfs_poly.cpp` 里的 `deadline_per_coeff`）。整窗调用时一次 specialq 覆盖
整个窗口，起点就是整窗起点，判据恰好等价于"累计耗时 > 总预算"。

分块时同一个 a_d 要调 N 次 specialq，**每次各自取起点**，于是第 k 块的
判据变成"本块花了 > 总预算"——每块都能再花满一整份预算。代数上：

```
修复前：块 k 停在 (t - tk) > B        即累计耗时 > (tk - T0) + B
        块越靠后允许的总耗时越长，最坏 N * B
修复后：块 k 停在 (t - tk) > (dead - tk)  即 t > dead
        即累计耗时 > B                  与整窗调用一致
```

真正的兜底只剩块边界的 `deadline_end` 检查（`stage1.cpp` 的分块循环），
它只在块与块之间生效，块内跑飞的部分拦不住。

**实测（2026-10-20，100 位半素数，a_d=1000，p[1000,2400]，q[1000,1e8]，
cpu_gerbicz 引擎，机器空闲）**

修复前（`coeff_deadline=10`，4 轮）：

| 配置 | 耗时 |
| --- | --- |
| 不分块 | 10.64 / 10.64 / 10.61 / 10.61 秒 |
| 分块 块=2e6（50 块） | **13.50 / 13.87 / 13.75 / 14.02 秒** |
| 分块 块=1e8（1 块） | 10.60 / 10.59 / 10.59 / 10.61 秒 |

修复前分块比 deadline 多了 33%。块=1e8 时只有一次 specialq 调用，起点
就是整窗起点，语义退化成正确行为，所以与不分块一致 —— 这证明差异来自
块数，不来自别的地方。

修复后（`coeff_deadline=10`，3 轮）：

| 配置 | 耗时 |
| --- | --- |
| 不分块 | 10.69 / 10.67 / 10.66 秒 |
| 分块 块=2e6（50 块） | 10.62 / 10.71 / 10.67 秒 |

deadline 语义恢复正确，两者完全持平。

**修法。** `task_data_t.coeff_deadline` 从 `uint32` 改成 `double`，
分块循环里每次调用 specialq 之前把它刷新成"剩余预算"：

```c
double remaining = deadline_end - get_wall_time();
if (remaining <= 0)
    return;
task->coeff_deadline = remaining;
v->specialq(task, threadid, chunk_lo, chunk_hi, p_min, p_max);
```

必须是 double：剩余不足 1 秒时 `uint32` 会截成 0，而 cpu 引擎的判据第一个
条件是 `(task->coeff_deadline && ...)`，值为 0 会让整个 deadline 检查被
**跳过**，比原来更糟。

**但结论没变：分块仍然不该启用。** 修好之后，分块在 CPU 引擎上买到的东西
仍然只有 deadline 的及时性，而这件事整窗调用已经由引擎内层每 16384 个
q-root 的检查做到了（`stage1_sieve_cpu.cpp:1349-1365`）。剩下的只有每次
specialq 调用重建全部 p 的等差数列这份开销
（`stage1_sieve_cpu.cpp:1235-1242`），与 q 窗口大小无关。

**这份开销有多大，直接测 p 数列重建的耗时**（单独写了个程序复刻
`stage1_sieve_cpu.cpp:1237-1242` 那三行，计时 20~30 轮取最优）：

| p 范围 | 重建一次 | 50 块多付 49 次 | 20 块多付 19 次 |
| --- | --- | --- | --- |
| p[1000, 2400]（基准用的） | 0.036 ms | 1.76 ms | 0.68 ms |
| p[1000, 10000] | 0.49 ms | 24.2 ms | 9.4 ms |
| p[10000, 24000] degree 4 | 1.16 ms | 56.6 ms | 22.0 ms |
| p[10000, 24000] degree 5 | 1.45 ms | 70.9 ms | 27.5 ms |
| p[100000, 240000] degree 4 | 12.1 ms | 593 ms | 230 ms |
| p[100000, 240000] degree 6 | 17.2 ms | 845 ms | 328 ms |

开销随 p 范围线性增长：p 范围大 100 倍，重建成本也大 100 倍。真实调用
里 `p_max = MIN(MAX_P, sqrt(p_size_max))`、`p_min = p_max / P_SCALE`
（`stage1.cpp:672-675`），大任务上 p 范围可以到 1e5 量级，那时 50 块要多付
半秒到近一秒。

**上一版"分块慢 1.9%"的数字不可靠**：那是在有其它任务并行的机器上测的，
100 秒级任务本身的噪声就有 ±3 秒，而真实开销是毫秒到亚秒级的。

顺带修掉了分块路径在 `coeff_deadline=0`（用户显式表示"不限时间"）时的
另一个毛病：`deadline_end` 会被算成"起点 + 0"，块边界的检查立刻成立，
分块版只筛了第一块就退出（1/50 的窗口）。现在有预算才走剩余预算那套
逻辑，无预算就保持 0 不变，让引擎自己按"不限"处理。

### stage 1 的分块：结论是不该启用（已实测确认）

上一轮把"让素数区间与配对生成器对上，新的一段能接着上一段继续"这条留成
了待定。这一轮按代码把它定下来：**在 CPU 引擎上分块不该启用。**

这一节同时把上一版的两条依据换成了实测数据 —— 上一版写"分块让 deadline
的响应粒度粗 122 倍"是错的：那个 16384 粒度的检查在
`stage1_specialq_cpu()` **函数内部**（`stage1_sieve_cpu.cpp:1349-1365`），
分块之后每次 chunk 调用仍然会跑它，分块并不替代它。

**正确的依据有三条：**

1. **分块唯一买到的东西，整窗调用已经做到了。** CPU 引擎的 specialq 实现
   在内层每 **16384** 个 q-root 查一次墙钟并对照 `task->coeff_deadline`，
   超时就 `goto finished`。分块不替代这个检查，只是在块边界再加一次更粗
   的检查。
2. **分块要付的固定成本是真的。** 每次 `specialq` 调用都要重建全部 p 的
   等差数列（`stage1_sieve_cpu.cpp:1235-1242`：`p_packed_reset` +
   `sieve_fb_reset` + 遍历 `sieve_fb_next`），这部分与 q 窗口大小无关；
   分块把 1 次调用变成 `STAGE1_QCHUNK_DIVISOR = 20` 次。
3. **分块代码本身是关着的。** `stage1.cpp:739-741` 的 `if (0)` 关着分块
   分支，活跃路径是 `stage1.cpp:799-814` 的整窗调用。

**实测（2026-10-20）。** 上一版说"基准通道没打通"，这一轮打通了：yafu 自带
的 stage1 test_mode 通道（`obj->nfs_args` 里的 `test_ad=` / `test_pmin=` /
`test_pmax=` / `test_qmin=` / `test_qmax=`，由 `-nfs_stage1_args` 传入）
可以完全固定一次筛选的工作量 —— 100 位半素数、`a_d=1000`、`p[1000,2400]`、
`q[1000,1e8]`、`cpu_gerbicz` 引擎，命中数写成 `test_cpu_gerbicz.hits`。
把 `stage1.cpp` 的 `if (0)` 临时换成环境变量开关，分别构建两个 yafu 对比：

| 场景 | 不分块 | 分块 |
| --- | --- | --- |
| `coeff_deadline=10` | 10.28 秒停，7527 命中 | 10.27 秒停，7328 命中 |
| 不限 deadline（两轮） | 95.94 / 95.77 秒，51388 命中 | 97.70 / 98.14 秒，51388 命中 |

两行读出来的意思：

- **及时性上没有区别**（10.28 vs 10.27）—— 分块唯一想买到的东西，整窗调用
  已经做到了。
- **总耗时分块慢 1.9%**（97.70 / 98.14 vs 95.94 / 95.77），命中数四轮全部
  是 51388 —— 分块是纯开销，多付的正是那 19 次 p 等差数列重建。

另有两点支持这个结论：段是按素数个数切的（一次 `PRIME_RANGE = 1e8` 个
素数），B2 通常 1e4~1e5 量级，段循环通常只跑一轮 —— 根本没有"新的一段"；
就算有多段，一段覆盖的 a 跨度 `(B2-B1)/(2w)` 与 Pa 表覆盖的 `2*L = 4*U`
（U 的候选只有 1..16）差好几个数量级，新旧段的 Pa 没有交集。

GPU 引擎本来就保持整窗调用（`stage1.cpp:757-760`）—— 它的 deadline 检查在
设备侧，分块对它没有意义。

于是 `STAGE1_QCHUNK_DIVISOR` / `STAGE1_QCHUNK_MAX` 这两个常量没有要定的
价值：三份计划文档里对应的条目从"确实待定"改成了"不适用"。

### 精读全树：五个确凿的问题

这一轮把整棵树又过了一遍（自有代码 24 万行），找算法里的逻辑错误和
工程上的瑕疵。找到五个确凿的问题，都已修。

**1. 32 位 APR-CL 是死代码，而且带着一个随时会炸的符号冲突。**

`factor/shared/aprcl/mpz_aprcl32.cpp`、`include/mpz_aprcl32.h`、
`include/jacobi_sum32.h` 一共 8434 行，是 msieve 为 32 位平台准备的。
它跟 64 位的 `mpz_aprcl.cpp` 导出**同名**的全局符号 ——
`mpz_aprcl32.o` 的每个函数符号在 `mpz_aprcl.o` 里都有同名同签名的版本
（32 位独有符号集为空）。而且两边的查表数据并不一样：

| 符号 | 64 位 | 32 位 |
| --- | --- | --- |
| aiQ | 618 项 | 530 项 |
| aiG | 618 项 | 530 项 |
| aiNP | 9 项 | 8 项 |
| aiNQ | 9 项 | 8 项 |
| Qmax | 9 项 | 8 项 |

这些是全局符号，一旦两个目标文件同时被链接，链接器只保留一份，另一边
就会按错误的表长访问、越界读，查表结果也可能错。

现在还没炸：`mpz_aprcl32.o` 没有被链进任何可执行文件 —— `mp.cpp` 引用
的是 `mpz_aprcl`，而 `mpz_aprcl.o` 先满足了那个引用。但只要有人引用
32 位版独有的符号，就会变成 multiple definition 链接错误。

yafu 早已只面向 Linux x86-64（`342f9d0` 删光全部非 x86-64 分支），
这三个文件零引用，已整组删掉。`mp.cpp` 改用 `mpz_aprcl.h`（64 位版的
超集，声明齐全），`Makefile` 去掉该源文件。

**2. `process_rel` 漏了一个截断检查。**

上一轮给 `filter.cpp` 的 `lp[0]` 加了截断检查（数据文件被截断时
`lp[0]` 会静默变成 0），但同一函数的 `lp[1]` 没有。`lp[1]` 变成 0 之后
会被当成一个真的大素数加进 cycle 图 —— 图里多出一个素数 0 的顶点，环
计数随之偏差。这次补上了。

第 2 条配了一条回归测试（`test/layer3/test_qs_review.cpp` 的
`siqs_process_rel_truncated`），判据用哨兵值：把 `rel->large_prime[1]`
预置成 `0xDEADBEEF`，截断的行在早退时不会碰它（赋值在所有截断检查
之后），而没被截断的行会走到那里写上真正的 `lp[1]`。返回值区分不了"因
截断被拒"和"因关系校验失败被拒"——两者都是 1，所以不能只看返回值。

这条测试的写法前后改了三轮才真正有效：第一版 `sconf->bpoly_alloc`
没初始化，所有用例都在同一个检查上早退，把 `lp[1]` 的检查去掉照样通过；
第二版 `ensure_bpoly` 读没准备的 `bpoly_gray` 导致段错误；第三版补上
`bpoly_gray` / `bpoly_nu` / `bpoly_Bl` 与一个清零的 `fact_obj_t`（`check_relation`
要从它取 `VFLAG`）之后，去掉 `lp[1]` 的检查会明确报出断言失败。

**3. `nfs_thread_task` 里有 5 个从来没被赋值过的枚举值。**

`TASK_NP1` / `TASK_NPS` / `TASK_NPR` / `TASK_NP1_GPU` / `TASK_SIEVE`
全树没有任何地方给它们赋值 —— 老版本用它们给 `-np1`/`-nps`/`-npr`
分阶段派工作，现在换成了 `nfs_phases` 位标志加 `special_polyfind` 标志。
留着它们只会让 `nfs_poly.cpp` 的两处 switch 漏分支（编译器报了 12 条
`-Wswitch`）。已删掉，并给两处 switch 补上 `TASK_DONE` 分支
（这个在用，语义就是"没有状态要同步"）和 `default` 分支（未知任务类型
报出来，不静默吞掉）。

**4. `factor_perfect_power` 漏掉两个 `mpz_clear`。**

`factor/trialdiv/trialdiv.cpp` 里，日志文件打不开时直接 `return`，
跳过了函数末尾的 `mpz_clear(base)` / `mpz_clear(ans)` —— 而这两个
`mpz` 在函数开头已经 `mpz_init` 过。每开一次日志失败就漏一份 GMP 大数。

同一文件里另外几处相同的"fopen 失败就 return"写法都没有这个问题：
`zTrial` 与 `zFermat` 在 return 之前没有 `mpz_init`，`spfermat` 走的是
`goto done`，`rho.cpp` 干脆不 return（`flog` 保持 NULL 继续跑）。

**5. `recurrence6.cpp` 里读未初始化的 `__m512i`。**

`get_recurrence_info_16` 的两个变体里都有：

```c
for (;;) {
    __m512i k;                                 // 未初始化
    ...
    k = _mm512_mask_add_epi64(k, m & bA, ...);   // k 是 merge 的 src
```

`k` 是 merge 指令的"被覆盖源"：mask 位上的位置用新值，非 mask 位置保留
`k` 的原值 —— 而 `k` 从没被初始化过。实际结果不受影响（那些位置最终
不会被用到），但读未初始化的 `__m512i` 是未定义行为，编译器无法证明
这一点，GCC 16 直接报 `-Wuninitialized`。把 `k` 初始化成零向量即可，
未初始化值本来就不参与任何有效结果。

### 性能：先把热点量出来，再决定动哪里

这一轮先量了 SIQS 路径的耗时分布（90 位半素数，6 逻辑核 / 3 物理核的机器，
`-v` 输出的阶段计时；CPU 时间是各线程累加，墙钟 81.05 秒、CPU 337 秒）：

| 阶段 | CPU 秒 | 占比 |
| --- | --- | --- |
| Poly update（多项式根更新） | 132.8 | 39% |
| Med sieve | 85.6 | 25% |
| Tdiv（试除） | 65.7 | 19% |
| Large sieve | 53.4 | 16% |
| Lanczos | 7.4 | 2% |
| Sqrt | 0.2 | 0.06% |

热点全在手写 AVX512 intrinsic 里（`update_poly_roots_32k_knl.cpp`、
`med_sieve_32k_avx2.cpp`）。本来打算用 C++26 的 `std::simd` 重写，但
GCC 16.2 还没有实现它 —— `<simd>` 头文件在，`std::simd` 实体没有
（`__cpp_lib_simd` 未定义）。所以这条路走不通，热点的 SIMD 代码动不了。

**两个看起来有希望、实测后被否掉的优化**（都先做了微基准再决定）：

1. **把关系去重的排序改成"排下标 + 一次重排"**。`siqs_r` 有 836 字节
   （`fb_offsets[200]`），而比较只用到大素数、因子数和前几个
   `fb_offsets`；`qsort` 的交换次数约 n·log n，每次搬 836 字节，
   40 万个关系就是 6 GB 以上的内存流量。改成排 `uint32` 下标（交换
   4 字节）再重排，理论上能省一个数量级。

   **实测反而慢 2.8 倍**（0.174 秒 -> 0.483 秒）：`std::sort` 的每次比较
   要间接访问 `rlist`（随机访问 836 字节的元素，缓存未命中），而
   `qsort` 直接在数组上排序。顺带量出：40 万个关系的排序本身只要
   0.17 秒 —— **排序根本不是瓶颈**，过滤阶段的 64.8 秒在别处。

2. **省掉试除的重复劳动**。内存模式下 `td_and_merge_relation` 会对每条
   关系重新试除一遍小素数，而筛法阶段（`tdiv.cpp:1200`）已经算过并填在
   `rel->fb_offsets` 里了。

   **不成立**：`sconf->small_limit = 256`（`SIQS.cpp:3811`），所以
   `sieve_small_fb_start` 只是小于 256 的素数个数 —— 54 个。重复的只有
   54 次试除，不值得改。Tdiv 那 19% 是筛法阶段的大素数试除，是本质
   工作。

   顺带把一条更激进的思路也否掉了：能不能用 Q 的低 64 位做试除判断
   （`p | Q_lo` 就能断定 `p | Q`）？不行 —— `p | Q_lo` **推不出**
   `p | Q`（反例 Q = 2^64 + 3、p = 3 时 Q_lo = 3 但 3 不整除 Q）。
   而 `Q mod p` 需要 Q 的全部位，`mpz_tdiv_ui` 已经是 GMP 的最优实现。

**线程数：默认值（逻辑核心数）是对的。** 90 位半素数、SIQS 路径，
每轮之前删掉 savefile 保证从零开始（6 逻辑核 / 3 物理核的机器）：

| threads | 墙钟（两轮） | SIQS 段 |
| --- | --- | --- |
| 3（物理核） | 118.17 / 114.48 | 105.9 / 102.5 |
| 4 | 95.04 / 95.51 | 85.5 / 85.9 |
| **6（默认，逻辑核）** | **83.71 / 84.69** | 75.5 / 76.3 |
| 8（6 核机器上过量） | 81.04 / 84.19 | 71.5 / 75.5 |

超线程对 SIQS 是**有利**的（6 线程比 3 线程快 30%），与"AVX512 筛法
吃内存带宽、超线程只会争抢"这个直觉相反 —— 6 线程比 3 线程快 30%。
默认的 `ytools_get_nprocs()`（`sysconf(_SC_NPROCESSORS_ONLN)`，逻辑
核心数）正好落在这个区间，不用改。线程数超过物理核数后（8 线程在 6 核
机器上）没有收益也没有明显退化。

**测量方法上踩的两个坑**（都记下来，免得下次再犯）：

- `perf` 在这台 WSL 上采不到样本（`perf_event_paranoid=2` 且缺
  libunwind/libdw），`gprof` 需要 `-pg` 重编译但会覆盖掉 Makefile
  里的 SIMD 选项而编译失败。yafu 自己的阶段计时（`Med sieve time` /
  `Poly update time` 等）反而够用。
- **SIQS 的 savefile（`siqs.dat`）会让后续运行续算**，同一输入第二次
  跑只要 15 秒而不是 123 秒。做基准必须每轮之前删掉它，否则数据全是
  假的。第一版线程数测试就栽在这上面。

### C++26 特性：能用哪些、还差哪些

用 GCC 16.2 逐个编译探测过（每个特性单独一个小程序，能编过就算支持）：

| 特性 | 状态 |
| --- | --- |
| `std::move_only_function`（C++26） | 可用 |
| `std::inplace_vector` | 可用（线程池的回调表在用） |
| `std::copyable_function` | 可用（线程池在用，18 处） |
| `std::string_view` | 可用（28 处） |
| `std::expected` | 可用（9 处） |
| `std::source_location` | 可用 |
| `std::print` / `std::stacktrace` | 可用 |
| `std::flat_map` / `std::flat_set` | 可用 |
| `std::span` / `std::optional` / `std::variant` | 可用 |
| `std::unreachable` / `std::to_underlying` / `std::is_constant_evaluated` | 可用 |
| `consteval` / `= delete("reason")` | 可用 |
| `std::simd`（C++26） | **不可用** —— `<simd>` 头文件在，`std::simd` 实体没有 |
| `std::function_ref`（C++26） | **不可用** |
| `std::text_encoding`（C++26） | **不可用** |
| `std::mdspan` / `std::generator` / `std::spanstream` / `std::ranges::to` | **不可用** |
| `std::byteswap` / `std::bit_cast` / `std::out_ptr` | **不可用** |

几个结论：

- **`std::simd` 用不了**，所以手写 AVX512 intrinsic 那部分（占 SIQS 的
  64%）没法换成可移植写法 —— 也就没法用 C++26 的方式去优化它。
- **`std::flat_map` 没有应用点**：这棵树的自有代码里 `std::map` /
  `std::unordered_map` 一次都没用过（全是自制的哈希表）。
- **`std::span` 的应用点收益不大**：能改的地方都是"指针 + 长度"参数对，
  改成 `span` 主要是接口好看，跨文件的签名改动多，收益不抵风险。
- **`std::scoped_lock` 这类 RAII 锁值得用，但本树用的是 `pthread_mutex_t`，
  不是 `std::mutex`**，套不上 `std::unique_lock`；要换就得把
  `pthread_cond_t` 也换成 `std::condition_variable`，那是一次贯穿线程池
  和 NFS 线程的改动，不该混在这一轮里做。

**这一轮实际改掉的"该用 RAII 却是手写 lock/unlock"的地方**：没有。手写的
lock/unlock 我逐处核对过加解锁配平，包括看起来最可疑的
`nfs_stop_worker_thread`（`is_master_thread == 0` 时 start 函数故意不解锁，
锁由主线程持有到 stop，stop 里那次 unlock 是配平的）。

### 剩下的 TODO 标记：清零

上一轮结束时，代码里还剩 12 处 TODO / FIXME / XXX 标记。这一轮逐处处理
完，代码里一处不剩；`factor/nfs/gnfs/poly/` 下三份计划文档里的 23 条
"open items" 也逐条对着代码核过，每条落了状态。

12 处的去处：

- `factor/shared/aprcl/mpz_aprcl.cpp` 与 `mpz_aprcl32.cpp`（各一处）：
  `JacobiSum` 查表失败时静默算出错误的答案，现在报出缺的是哪一项并停下。
- `factor/shared/aprcl/tinyprp.cpp`：Lucas 自测的下标与 mpz 版对齐；
  只支持 p == 1 却接受任意 p 的那个函数全树没有调用点，连定义一起删掉。
- `factor/shared/cub/cub/util_debug.cuh`：不是待办，是 clang 当 CUDA
  主机编译器时用的变参转发，措辞改成说明。
- `factor/ecm/avxecm.cpp`（两处）：Suyama 的 d == -1/8 那条随死函数一起
  消失；stage 2 配对接续那条按现在的段划分没有可实现的形态，把数字与
  原因写进了注释。
- `factor/nfs/nfs.cpp`（两处）：CADO 的输出重定向不再走 shell 语法；
  多因子的输入改成"没有合数剩余时才停"（见
  `factor/nfs/gnfs/sqrt/sqrt.cpp`）。
- `factor/nfs/nfs_sieving.cpp`：续算时用的块大小落进 `.job` 文件。
- `factor/nfs/lasieve/mpqs3.cpp`：平方根的逆元在生成因子基时一次算好；
  "批量模逆"那条不成立（各项的模数互不相同），注释改准确。
- `factor/nfs/lasieve/kernels/include/mpqs-config.h`：把 15 行的分档
  规则、7 列的含义与影响范围写进表头注释。
- `factor/siqs/filter.cpp`：最后一个 B 值从未被赋值（真 bug），以及 B 值
  改成按需生成。


### 线程池：回调表换成 C++26 的 std::inplace_vector，并禁止拷贝

`factor/shared/ytools/include/threadpool.h` 的工作函数表原来是一个
`std::copyable_function<void(void*)> tpool_work_fcn[16]` 裸数组，外加一个
手工维护的 `num_work_fcn` 计数器。两份状态可能对不上。现在表本身换成
C++26 的 `std::inplace_vector`（上限 16，不需要堆分配），长度由容器自己
记着，`num_work_fcn` 这个字段没有了 —— 18 处引用点改成问容器要。

`tpool_t` 里装着 `pthread_mutex_t` 与 `pthread_cond_t`，拷贝它等于把锁也
复制一份，两份锁互不同步，析构时还会重复解锁。现在显式删掉拷贝构造与
拷贝赋值，并写明原因（C++26 的 `= delete("reason")`，编译器的诊断信息里
会带这句话）。

### 新的随机对拍测试

`test/layer1/test_tinyprp_review.cpp` 新增四个测试，把 tinyprp 的标量与
AVX512 路径逐个和 GMP 参考实现比对：

* `random_64bit_mr`：2000 个随机 64 位奇数，`MR_2sprp_64x1` 对 GMP。
* `random_128bit_prp`：1000 个随机 128 位奇数，Fermat / Miller-Rabin /
  Selfridge / BPSW 四种对 GMP。
* `random_128bit_lucas`：1000 个随机 128 位奇数配上 Selfridge 的 d 序列，
  Lucas 自测对 `mpz_lucas_prp`。
* `random_104bit_avx512`：400 组 × 8 路的 104 位数，AVX512 的
  `MR_2sprp_104x8` 对标量的 `MR_2sprp_128x1`。

`test/standalone/lasieve/process_batch_helpers_regression.cpp` 也补了一条：
四个大素数槽都要写出来，值为 1 的槽跳过，64 位值按十六进制完整写出。

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

脱离上游独立维护的起点：以当前上游 `bbuhrow/yafu` 的 `963dbe9`（合并 Gerbicz 碰撞多项式选择）为起点，完成下列整理。

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
