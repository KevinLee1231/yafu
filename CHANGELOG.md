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

## 1.0.0

建立自用基线：以当前上游 `bbuhrow/yafu` 的 `963dbe9`（合并 Gerbicz 碰撞多项式选择）为起点，完成下列整理。

### 架构

目录按分解方法重组。每个方法一个以方法命名的子目录，实现与该方法私有的头文件分开放在 `include/`：

- `factor/core/` 调度层，决定每个数走哪条分解路线
- `factor/trialdiv/` 试除、Fermat、Pollard rho、SQUFOF、Lehman
- `factor/siqs/` 自初始化二次筛
- `factor/ecm/` 椭圆曲线分解，标量与 AVX-512 两套实现合并到一处
- `factor/nfs/` 作业编排，`factor/nfs/gnfs/` 数域筛本体
- `factor/siqs/mpqs/` 多项式二次筛
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
