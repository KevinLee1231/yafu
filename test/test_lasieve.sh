#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-gcc}
cflags=${CFLAGS:--std=gnu17 -O0 -g -Wall -Wextra}
# 筛法器和测试驱动现在都是 C++。
cxx=${CXX:-g++}
cxxflags=${CXXFLAGS:--std=c++26 -O0 -g}
build_dir=$(mktemp -d /tmp/yafu-lasieve-test.XXXXXX)

cleanup()
{
    rm -rf "$build_dir"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

cd "$repo_root"

INC="-I. -Ifactor/nfs/lasieve -Ifactor/nfs/lasieve/asm \
-Ifactor/nfs/lasieve/include -Ifactor/nfs/lasieve/asm/include \
-Ifactor/shared/include -Ifactor/ecm/include -Ifactor/shared/ytools/include \
-Ifactor/shared/aprcl/include"

# 编译测试驱动
compile()
{
    "$cxx" $cxxflags -D_GNU_SOURCE -UNDEBUG "$@"
}

# 编译一个筛法器源，输出对象路径到 stdout。
# 第一个参数是输出标签，第二个是源文件，其余是额外选项 —— 标签不能省，
# 否则同一份源用不同选项编两次会写到一个对象上，后一次覆盖前一次。
compile_cxx()
{
    tag=$1
    src=$2
    shift 2
    obj="$build_dir/$tag-$(basename "$src" .cpp).o"
    "$cxx" $cxxflags -D_GNU_SOURCE -UNDEBUG $INC "$@" -c "$src" -o "$obj"
    printf '%s' "$obj"
}

BAIL_OBJ=$(compile_cxx obj factor/nfs/lasieve/lasieve_bail.cpp)
IF_OBJ=$(compile_cxx obj factor/nfs/lasieve/if.cpp)
GMP_AUX_OBJ=$(compile_cxx obj factor/nfs/lasieve/gmp-aux.cpp)
REDU2_OBJ=$(compile_cxx obj factor/nfs/lasieve/redu2.cpp)
INPUT_POLY_OBJ=$(compile_cxx obj factor/nfs/lasieve/input-poly.cpp)

compile $INC \
    test/standalone/lasieve/core_regression.cpp \
    "$BAIL_OBJ" "$IF_OBJ" "$GMP_AUX_OBJ" "$REDU2_OBJ" \
    -lgmp -lm -o "$build_dir/core_regression"
"$build_dir/core_regression"

compile -DNEED_ASPRINTF $INC \
    test/standalone/lasieve/asprintf_regression.cpp \
    "$BAIL_OBJ" "$IF_OBJ" \
    -lgmp -o "$build_dir/asprintf_regression"
"$build_dir/asprintf_regression"

# batch_tree_regression 和 process_batch_helpers_regression 直接 #include 了
# 被测源文件本身（为了够到里面的 static 函数），所以它们必须和被测源用同一种
# 语言编译 —— 现在被测源是 C++，这两个驱动也按 C++ 编。
#
# batch_factor.cpp 里的 xrealloc 走的是 yafu 那一路：util.h 用
# YA_ALLOC_DECLARED 和 ytools.h 二选一，先到的提供实现，所以符号是 C++ 链接的
# _Z8xreallocPvm，定义在 factor/shared/ytools/ytools.cpp。筛法器 if.cpp 里另有
# 一份同名实现，走 if.h 的 extern "C"，是给筛法器内部用的 —— 两者互不干涉，
# 但驱动要链上前者，所以 ytools.o 得一起链上。
ASAN_OPTS="-fsanitize=address -ffunction-sections -fdata-sections"
ASAN_BAIL=$(compile_cxx asan factor/nfs/lasieve/lasieve_bail.cpp $ASAN_OPTS)
ASAN_IF=$(compile_cxx asan factor/nfs/lasieve/if.cpp $ASAN_OPTS)
ASAN_YTOOLS=$(compile_cxx asan factor/shared/ytools/ytools.cpp $ASAN_OPTS)
compile $INC $ASAN_OPTS \
    test/standalone/lasieve/batch_tree_regression.cpp \
    "$ASAN_BAIL" "$ASAN_IF" "$ASAN_YTOOLS" -Wl,--gc-sections -lgmp -lm \
    -o "$build_dir/batch_tree_regression"
ASAN_OPTIONS=detect_leaks=1 "$build_dir/batch_tree_regression"

compile -Wformat=2 $INC \
    test/standalone/lasieve/input_poly_regression.cpp \
    "$BAIL_OBJ" "$IF_OBJ" "$INPUT_POLY_OBJ" \
    -lgmp -o "$build_dir/input_poly_regression"
"$build_dir/input_poly_regression"

compile -ffunction-sections -fdata-sections $INC \
    test/standalone/lasieve/process_batch_helpers_regression.cpp \
    -Wl,--gc-sections -lgmp -o "$build_dir/process_batch_helpers_regression"
"$build_dir/process_batch_helpers_regression"

# 筛法正确性基准：固定多项式 + 固定 spq 区间，六个 I 值的关系集合必须与基准一致。
#
# 筛法器是 yafu 的一部分，make all 只产出 yafu 一个可执行文件；这里用
# lasieve/Makefile 的 check_sieve 目标现编一个临时驱动（同一批对象），
# 测完随 build_dir 一起删掉。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/check_sieve" \
    BINDIR="$build_dir" CC="$cc" CXX="$cxx"
sh "$repo_root/test/standalone/lasieve/sieve_oracle.sh" "$build_dir/check_sieve"

# 六个 I 值同时跑，各占一个线程。sieve_oracle.sh 一次只跑一个 I 值，而且每次都是
# 新进程，所以它看不到两类只有同进程多次调用才暴露的问题：
#   * getopt 的 optind 是进程级全局且不自己复位，第二次进 main 会跳过 -a/-f
#   * 两个筛法器共用 factor base 或蒙哥马利状态
# 每线程一份多项式副本，筛法器按输入名派生自己的附属文件，互不踩。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/check_sieve_mt" \
    BINDIR="$build_dir" CC="$cc" CXX="$cxx"
sieve_dir=$(mktemp -d "${TMPDIR:-/tmp}/yafu-sieve-mt.XXXXXX")
cp "$repo_root/factor/nfs/lasieve/R942_poly.txt" "$sieve_dir/poly"
( cd "$sieve_dir" && "$build_dir/check_sieve_mt" poly 650000 20 6 )
rm -rf "$sieve_dir"

# 多实例验证：ECM/PM1 的缓存搬进 lasieve_ctx 之后，两个实例交替推进必须
# 和各自单独跑出一样的结果。搬到 ctx 之前是文件级全局，这项会挂。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/ctx_test" \
    BINDIR="$build_dir" CC="$cc" CXX="$cxx"
"$build_dir/ctx_test"

# ECM/P-1 的数值正确性 + 两个实例交错推进的隔离性。
# 这两条路径的位图访问曾按字节下标算而缓冲区按 u64 字分配，越界到缓冲区外
# 8 倍处；仓库里没有别的测试走到它们，靠这项守住。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/ecm_pm1_test" \
    BINDIR="$build_dir" CC="$cc" CXX="$cxx"
"$build_dir/ecm_pm1_test"

printf '%s\n' 'lasieve standalone tests passed'
