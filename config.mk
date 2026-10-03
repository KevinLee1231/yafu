# =============================================================================
# config.mk — yafu 本地构建配置
#
# 目标机器: Kali Linux (WSL2) / AMD Ryzen AI 7 H 350 (Zen 5, 6 核)
# 生成时间: 2026-09-18
#
# 注意: 不要在本文件的赋值行末尾写行内注释 —— make 会把注释文本并入变量值。
# =============================================================================

# --- GMP (必需) --------------------------------------------------------------
# 装在 Debian multiarch 路径下。Makefile 默认的 SYS_INC_PATHS 为
# /usr/include /usr/local/include /opt/local/include，不包含 multiarch 子目录，
# 因此不显式指定就会报 "GMP is required but gmp.h was not found"。
# 头文件实际位置: /usr/include/x86_64-linux-gnu/gmp.h
GMP_INCDIR := /usr/include/x86_64-linux-gnu


# --- ISA 优化 ----------------------------------------------------------------
# 关键结论: 官方 CI 从不使用 USE_NATIVE。本机实测 USE_NATIVE=1 会产出筛法失效的
# 二进制 —— 能编译、能跑小整数，但 SIQS 几乎筛不出关系（"极慢"而非报错）。
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
#   Build 4  旧版 yafu 3.1.2 二进制 (对照)
#            %zmm=0, 84703 条关系/秒, SIQS 0.0412 秒  —— 正常
#
# Build 2 与 Build 3 之间只差 -march=native 一个变量，其余 (源码版本 / GMP_INCDIR /
# OMP / ECM) 完全相同，因此根因即 -march=native 本身。
#
# 关于机制 (尚未确定，以下只是候选解释):
#   - 关掉 yafu 自带的 AVX-512 内核并无帮助，所以问题不在 yafu 的手写 SIMD 代码。
#   - yafu 全局已带 -fno-tree-loop-vectorize -fno-tree-slp-vectorize (Makefile:446)，
#     所以"编译器把计算循环自动向量化了"这个解释不成立。
#   - 剩下两类可能: 源码里的潜伏 UB 被不同的优化决策激活；或 GCC 的 znver5 后端问题。
#   - -march=native 在 Zen 5 上等价于 -march=znver5 -mtune=znver5，同时改变了
#     可用指令族、调度器/代价模型、EVEX 编码与扩展寄存器分配。
#
# ---------------------------------------------------------------------------
# AVX-512 / IFMA 可用性实测 (2026-09-19，官方 CI 写法，均不带 USE_NATIVE):
#   三个构建全部正确 — 完整分解同一 37 位数得到完全一致的结果。
#
#   构建            编译参数                 端到端   SIQS 探针(3轮均值)  ECM 内核耗时
#   AVX2            USE_AVX2=1 USE_BMI2=1    0.202s   82,983 条/秒        (GMP-ECM)
#   AVX512          USE_AVX512=1             0.189s   97,061 条/秒        1.34 秒
#   AVX512IFMA      USE_AVX512IFMA=1         0.166s   84,057 条/秒        0.88 秒
#
#   结论: 选择 IFMA。ECM 内核快 34% 且高度可复现 (0.8822/0.8893/0.8763 秒，
#   对照 AVX512 的 1.3549/1.3329/1.3287/1.3450 秒)；SIQS 上与 AVX512 的差距
#   落在探针噪声内 (探针 run-to-run 波动约 10-15%，区间大量重叠)。
#
#   注意: -march 由 Makefile 按档位自动搭配 — USE_AVX512=1 给 -march=skylake-avx512，
#   USE_AVX512IFMA=1 给 -march=icelake-client。两者都不是 native，都不触发上面那个故障。
#   换言之故障专属 -march=native，与 AVX-512 本身无关 —— 正常工作的 IFMA 构建里
#   有 7,928 条 vpmadd52 指令、26,370 处 %zmm 引用，比坏构建还多。
#
# 注: make info 的 "ISA flags" 显示栏对显式置 0 的开关会误报为 yes
#     ($(if $(USE_AVX512),...) 把字符串 "0" 判真)，以 CFLAGS 为准。
USE_AVX512IFMA := 1


# --- OpenMP ------------------------------------------------------------------
# 启用 OpenMP 线程支持。实际线程数在运行时由 yafu.ini 的 threads= 决定，
# 不开这个选项则完全没有多线程能力。
OMP := 1


# --- GMP-ECM (可选) ----------------------------------------------------------
# 头文件无需在此指定: Makefile 会在 SYS_INC_PATHS 中找到 /usr/include/ecm.h
# 并自动设为 HAVE_ECM_LIB (make info 的 "GMP-ECM: enabled")。
# 若显示为 disabled，说明 libecm-dev 未安装 (sudo apt install libecm-dev)。
#
# 但 ECM=1 是另一个开关，必须显式打开: 它才负责 -DHAVE_GMP_ECM 和链接 -lecm。
# 只装库不开它，结果是"库找到了却没链接进二进制"，ECM 分解功能实际不存在。
ECM := 1
