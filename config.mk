# =============================================================================
# config.mk — yafu 本地构建配置
#
# 目标机器: Kali Linux (WSL2) / AMD Ryzen AI 7 H 350 (Zen 5, 6 核)
# 更新日期: 2026-10-03
#
# 本文件按需覆盖 Makefile 的默认值；删掉整段就走 Makefile 的自动探测。
#
# 注意: 不要在本文件的赋值行末尾写行内注释 —— make 会把注释文本并入变量值。
# =============================================================================


# --- GMP (必需，无需设置) ----------------------------------------------------
# Makefile 会用 `cc -print-multiarch` 得出本机的 multiarch 元组，然后在
# /usr/include/<元组>、/usr/include、/usr/local/include、/opt/local/include
# 里依次找 gmp.h 和 libgmp.a。Debian/Ubuntu 的 /usr/include/x86_64-linux-gnu
# 会被自动命中，正常情况下这里什么都不用写。
#
# 只有当 GMP 装在非常规位置时才需要下面这行：
# GMP_INCDIR := /path/to/include


# --- ISA 档位 ---------------------------------------------------------------
# 这里只选 yafu 自带的 SIMD 内核走哪一档，-march 由 Makefile 按档位自动搭配
# （AVX512 -> skylake-avx512，AVX512IFMA -> icelake-client）。
#
# 关键结论: 不要用 USE_NATIVE。本机实测 USE_NATIVE=1 会产出筛法失效的二进制
# —— 能编译、能跑小整数，但 SIQS 几乎筛不出关系（表现为极慢而非报错）。
#
# 控制变量实测 (2026-09-18，被测数 37 位: 9000000000000000068000000000000000123)：
#
#   Build 1  USE_NATIVE=1 且 AVX-512 路径全开
#            "using AVX512BW enabled 32k sieve core", %zmm=27803
#            3.60 条关系/秒, SIQS 369.06 秒  —— 失败
#
#   Build 2  USE_AVX512=0 USE_AVX512IFMA=0，但仍 USE_NATIVE=1 (即仍有 -march=native)
#            "using AVX2 enabled 32k sieve core", %zmm=852
#            4.0 条关系/秒, SIQS 327.78 秒  —— 仍然失败
#
#   Build 3  USE_AVX2=1 USE_BMI2=1，无 -march=native
#            %zmm=0, 62002 条关系/秒, SIQS 0.0353 秒  —— 正常
#
# Build 2 与 Build 3 之间只差 -march=native 一个变量，其余 (源码版本 / GMP /
# 线程数 / ECM) 完全相同，因此根因即 -march=native 本身。
#
# 关于机制 (尚未确定，以下只是候选解释):
#   - 关掉 yafu 自带的 AVX-512 内核并无帮助，所以问题不在手写 SIMD 代码。
#   - 全局已带 -fno-tree-loop-vectorize -fno-tree-slp-vectorize，所以
#     "编译器把计算循环自动向量化了"这个解释不成立。
#   - 剩下两类可能: 源码里的潜伏 UB 被不同的优化决策激活；或 GCC 的 znver5
#     后端问题。-march=native 在 Zen 5 上等价于 -march=znver5 -mtune=znver5，
#     同时改变了可用指令族、调度器/代价模型、EVEX 编码与扩展寄存器分配。
#
# ---------------------------------------------------------------------------
# 三档横向对比 (2026-09-19，均不带 USE_NATIVE)，完整分解同一 37 位数，
# 结果完全一致:
#
#   档位             端到端   SIQS 探针(3轮均值)  ECM 内核耗时
#   USE_AVX2=1       0.202s   82,983 条/秒        (GMP-ECM)
#   USE_AVX512=1     0.189s   97,061 条/秒        1.34 秒
#   USE_AVX512IFMA=1 0.166s   84,057 条/秒        0.88 秒
#
#   选 IFMA: ECM 内核快 34% 且高度可复现 (0.8822/0.8893/0.8763 秒，对照
#   AVX512 的 1.3549/1.3329/1.3287/1.3450 秒)；SIQS 上与 AVX512 的差距落在
#   探针噪声内 (探针 run-to-wave 波动约 10-15%，区间大量重叠)。
#
#   正常工作的 IFMA 构建里有 7,928 条 vpmadd52 指令、26,370 处 %zmm 引用，比
#   坏构建还多 —— 再次印证故障专属 -march=native，与 AVX-512 本身无关。
USE_AVX512IFMA := 1
