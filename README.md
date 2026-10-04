# YAFU — 又一个整数分解工具

自动整数分解。

YAFU（借助其他自由软件）使用当代最强的算法及其实现，以完全自动化的方式分解输入整数。

YAFU 在学术文献中多次被引用。如果你的研究需要整数分解，YAFU 或许能帮上忙。

寻求分解帮助、技术支持与讨论，请到 <https://www.mersenneforum.org/node/58> 的社区。

> **详细文档在 [项目 wiki](../../wiki)。** wiki 涵盖每一个选项、每一个可调用函数，以及比下文更完整的构建说明。

---

## 快速开始

### 首次运行

```
yafu "factor(rsa(200))"
```

或者启动交互式提示符：

```
yafu
>> factor(2056802480868100646375721251575555494408897387375737955882170045672576386016591560879707933101909539325829251496440620798637813)
```

YAFU 从**当前工作目录**读取 `yafu.ini`，日志和中间文件也写在那里 —— 不是可执行文件所在的目录。Linux 上这两者通常不同，所以运行前先 `cd` 到放着 `yafu.ini` 的目录。无需任何安装步骤。

---

## 从源码编译

本仓库只面向 Linux，**按自己的 CPU 编译出来的二进制通常比通用版快得多**。整个构建由 `Makefile` 驱动，按机器调整的部分写在 `config.mk` 里。

### 依赖

| 必需 | 可选 |
| --- | --- |
| **GMP** — <https://gmplib.org/> | **CUDA 工具包**（GPU 余因子分解 / 多项式选择 / 线性代数） |
| **GMP-ECM** — <http://ecm.gforge.inria.fr/>（ECM 分解） | **OpenCL**（余因子分解的 CUDA 替代方案） |
| **OpenMP**（多线程 ECM 与筛选） | |

ECM 和 OpenMP 是无条件启用的：缺 `ecm.h` 会直接停止构建并说明该装什么。GMP 路径由 Makefile 按 multiarch 自动探测，通常不用手工指定。

`ysieve` 与 msieve 的实现都随仓库分发，作为 YAFU 构建的一部分一起编译，不需要另外安装。

### 编译与运行

```sh
make -j4 yafu
./yafu 'factor(91)' -terse
make -j4 lasieve          # 编译外部格点筛
make test-run             # 分层回归（算术 / 素性 / ECM / SIQS）
make test-cli             # 命令行回归
make test-standalone      # 独立编译的 NFS 与 lasieve 回归
```

`make info` 会打印最终解析出来的配置：GMP、ECM 的实际路径，以及选中的 CPU 指令集档位。

### 指令集档位

```bash
make yafu USE_AVX2=1          # 通用 x86-64 / Haswell 及更新
make yafu USE_AVX512=1        # Skylake-X、Cascade Lake、Zen 4
make yafu USE_AVX512IFMA=1    # Ice Lake 及更新，ECM 内核最快
make yafu CC=clang            # 换编译器
make yafu DEBUG=1             # 调试构建
```

**不要用 `USE_NATIVE=1`。** 本机实测它在 Zen 5 上会产出筛法失效的二进制：能编译、能跑小整数，但 SIQS 几乎筛不出关系（表现为极慢而不是报错）。根因是 `-march=native` 本身，与 AVX-512 无关 —— 选上面任一档位即可，Makefile 会自动搭配对应的 `-march`。完整的横向实测数据见 `config.mk` 里的注释。

编译器或编译选项变化会自动触发重编，不需要手工删旧的目标文件。

### 常用目标

| 目标 | 作用 |
| --- | --- |
| `yafu` | 主程序 |
| `all` | 同上（`all` 目前只构建 yafu） |
| `lasieve` | 外部格点筛（编译到 `factor/nfs/lasieve/bin/local/`） |
| `info` | 打印最终配置 |
| `help` | 功能开关速查 |
| `clean` | 清理构建产物 |

静态库等中间产物统一输出到 `build/`，仓库根目录不会产生生成文件。

---

## 目录结构

每个分解方法在 `factor/` 下占一个以方法命名的子目录，方法私有的头文件放在该目录的 `include/` 里；被多个方法共用的代码收在 `factor/shared/`，各保留一份。

| 目录 | 内容 |
| --- | --- |
| `factor/core/` | 调度层：决定每个数走哪条分解路线；也是命令行选项与 `yafu.ini` 的实现 |
| `factor/trialdiv/` | 试除、Fermat、Pollard rho、SQUFOF、Lehman |
| `factor/ecm/` | 椭圆曲线分解，标量与 AVX-512 两套实现 |
| `factor/nfs/` | NFS 作业编排 |
| `factor/nfs/gnfs/` | 数域筛本体：多项式选择、筛选、关系、线性代数、开方 |
| `factor/nfs/lasieve/` | 外部格点筛（NFS 必需） |
| `factor/siqs/` | 自初始化二次筛（SIQS，Contini 1997）。同一份代码里也带一份只面向小输入的精简版 `tinySIQS.c` |
| `factor/mpqs/` | 多项式二次筛（MPQS，Silverman, *Math. Comp.* 48 (1987) 329–339） |
| `factor/pmpqs/` | 首项系数取单个素数平方的多项式二次筛，与 MPQS 只差首项系数的取法；文献里没有名字 |
| `factor/shared/` | 共享代码：大数算术 `arith/`、素数筛 `ysieve/`、线程与线性代数 `common/`、`ytools/`、`cub/`、素性判定 `aprcl/` |
| `factor/shared/include/` | 全部头文件 |
| `top/` | 命令行前端 |
| `test/` | 三套回归测试 |
| `tools/` | 多项式搜索的离线分析脚本 |

`/usr/include` 之外的 GMP 位置、指令集档位等，在 `config.mk` 里覆盖；其余走 Makefile 自动探测。

---

## 持续集成

**GitHub Actions** 只在 Linux 上构建和测试：每次推送编译完整指令集矩阵（`generic`、`sse41`、`avx2`、`avx512`、`avx512ifma`），运行命令行回归脚本，并编译运行分层测试程序。`avx512` 档位只编译不运行 —— 托管的 runner 执行不了 AVX-512。打标签发布时产物会附到 release 页面。

---

## GGNFS 筛选器（NFS 必需）

NFS 分解需要外部的 GGNFS 格点筛程序（`ggnfs-lasieve4I*`）。仓库内带了预编译的 Linux 版本，在 `factor/nfs/lasieve/bin/` 下。用 `yafu.ini` 里的 `ggnfs_dir=` 或命令行 `-ggnfs_dir <路径>` 指向它们；没有它们 NFS 无法运行。

自己编译用 `make -j4 lasieve`，输出在 `factor/nfs/lasieve/bin/local/`。仓库里的 `yafu.ini` 指向这个目录，好让 NFS 用上你当前源码编译出的筛选器。这些本地产物不进 Git；预编译的那份仍可用 `-ggnfs_dir` 显式选择。

如果 CPU 支持 AVX-512，YAFU 会默认使用内置的 **AVX-ECM** 作为 ECM 后端。独立版本在 <https://github.com/bbuhrow/avx-ecm>。

---

## 帮助与文档

| 位置 | 内容 |
| --- | --- |
| YAFU 提示符里输入 `help` | 内置帮助（读 `top/docfile.txt`） |
| `help <函数名>` | 单个函数的详细说明 |
| `top/docfile.txt` | 函数参考 |
| `yafu.ini` | 全部选项，逐条注释 |
| [项目 wiki](../../wiki) | 以上全部，互相关联且可搜索 |
| <https://www.mersenneforum.org/node/58> | 社区与支持 |

---

## 例子

```
# 涉及 yafu 多种算法的例子
yafu "factor(2056802480868100646375721251575555494408897387375737955882170045672576386016591560879707933101909539325829251496440620798637813)"

# 涉及 ecm 与 siqs 的例子
yafu "factor(140870298550359924914704160737419905257747544866892632000062896476968602578482966342704)"
```

变更记录见 [CHANGELOG.md](CHANGELOG.md)。
