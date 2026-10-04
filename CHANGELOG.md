# yafu 变更日志

本项目自 2026-10-03 起脱离上游独立维护，版本号不再跟随 yafu 上游。

## 版本号规则

采用三段式 `主版本.次版本.修订号`：

| 段位 | 何时 +1 |
| --- | --- |
| 主版本 | 架构性改变：目录结构重排、构建系统重做、依赖项增减 |
| 次版本 | 新增分解算法，或既有算法新增实现路径（如 GPU 版本） |
| 修订号 | 修 bug、性能或健壮性改进，不改变外部行为 |

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
