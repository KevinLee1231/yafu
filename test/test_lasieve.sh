#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-gcc}
cflags=${CFLAGS:--std=gnu17 -O0 -g -Wall -Wextra}
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

compile()
{
    "$cc" $cflags -D_GNU_SOURCE -UNDEBUG "$@"
}

compile -I. -Ifactor/nfs/lasieve -Ifactor/nfs/lasieve/asm -Ifactor/nfs/lasieve/include -Ifactor/nfs/lasieve/asm/include -Ifactor/shared/include -Ifactor/ecm/include \
    test/standalone/lasieve/core_regression.c \
    factor/nfs/lasieve/lasieve_bail.c \
    factor/nfs/lasieve/gmp-aux.c factor/nfs/lasieve/redu2.c \
    factor/nfs/lasieve/if.c -lgmp -lm -o "$build_dir/core_regression"
"$build_dir/core_regression"

compile -DNEED_ASPRINTF -I. -Ifactor/nfs/lasieve \
    -Ifactor/nfs/lasieve/asm -Ifactor/nfs/lasieve/include \
    -Ifactor/nfs/lasieve/asm/include -Ifactor/shared/include -Ifactor/ecm/include \
    test/standalone/lasieve/asprintf_regression.c \
    factor/nfs/lasieve/lasieve_bail.c \
    factor/nfs/lasieve/if.c -lgmp -o "$build_dir/asprintf_regression"
"$build_dir/asprintf_regression"

compile -fsanitize=address -ffunction-sections -fdata-sections \
    -I. -Ifactor/nfs/lasieve -Ifactor/nfs/lasieve/asm -Ifactor/nfs/lasieve/include -Ifactor/nfs/lasieve/asm/include -Ifactor/shared/include -Ifactor/ecm/include \
    -Ifactor/shared/include -Ifactor/ecm/include -Ifactor/shared/ytools/include -Ifactor/shared/aprcl/include \
    test/standalone/lasieve/batch_tree_regression.c \
    factor/nfs/lasieve/lasieve_bail.c \
    factor/nfs/lasieve/if.c -Wl,--gc-sections -lgmp -lm \
    -o "$build_dir/batch_tree_regression"
ASAN_OPTIONS=detect_leaks=1 "$build_dir/batch_tree_regression"

compile -Wformat=2 -I. -Ifactor/nfs/lasieve \
    -Ifactor/nfs/lasieve/asm -Ifactor/nfs/lasieve/include \
    -Ifactor/nfs/lasieve/asm/include -Ifactor/shared/include -Ifactor/ecm/include \
    test/standalone/lasieve/input_poly_regression.c \
    factor/nfs/lasieve/lasieve_bail.c \
    factor/nfs/lasieve/input-poly.c factor/nfs/lasieve/if.c \
    -lgmp -o "$build_dir/input_poly_regression"
"$build_dir/input_poly_regression"

compile -ffunction-sections -fdata-sections -I. \
    -Ifactor/nfs/lasieve -Ifactor/nfs/lasieve/asm -Ifactor/nfs/lasieve/include -Ifactor/nfs/lasieve/asm/include -Ifactor/shared/include -Ifactor/ecm/include -Ifactor/shared/ytools/include \
    test/standalone/lasieve/process_batch_helpers_regression.c \
    -Wl,--gc-sections -lgmp -o "$build_dir/process_batch_helpers_regression"
"$build_dir/process_batch_helpers_regression"

# 筛法正确性基准：固定多项式 + 固定 spq 区间，六个 I 值的关系集合必须与基准一致。
#
# 筛法器是 yafu 的一部分，make all 只产出 yafu 一个可执行文件；这里用
# lasieve/Makefile 的 check_sieve 目标现编一个临时驱动（同一批对象），
# 测完随 build_dir 一起删掉。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/check_sieve" BINDIR="$build_dir" CC="$cc"
sh "$repo_root/test/standalone/lasieve/sieve_oracle.sh" "$build_dir/check_sieve"

# 六个 I 值同时跑，各占一个线程。sieve_oracle.sh 一次只跑一个 I 值，而且每次都是
# 新进程，所以它看不到两类只有同进程多次调用才暴露的问题：
#   * getopt 的 optind 是进程级全局且不自己复位，第二次进 main 会跳过 -a/-f
#   * 两个筛法器共用 factor base 或蒙哥马利状态
# 每线程一份多项式副本，筛法器按输入名派生自己的附属文件，互不踩。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/check_sieve_mt" BINDIR="$build_dir" CC="$cc"
sieve_dir=$(mktemp -d "${TMPDIR:-/tmp}/yafu-sieve-mt.XXXXXX")
cp "$repo_root/factor/nfs/lasieve/R942_poly.txt" "$sieve_dir/poly"
( cd "$sieve_dir" && "$build_dir/check_sieve_mt" poly 650000 20 6 )
rm -rf "$sieve_dir"

# 多实例验证：ECM/PM1 的缓存搬进 lasieve_ctx 之后，两个实例交替推进必须
# 和各自单独跑出一样的结果。搬到 ctx 之前是文件级全局，这项会挂。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/ctx_test" BINDIR="$build_dir" CC="$cc"
"$build_dir/ctx_test"

# ECM/P-1 的数值正确性 + 两个实例交错推进的隔离性。
# 这两条路径的位图访问曾按字节下标算而缓冲区按 u64 字分配，越界到缓冲区外
# 8 倍处；仓库里没有别的测试走到它们，靠这项守住。
make -s -C "$repo_root/factor/nfs/lasieve" "$build_dir/ecm_pm1_test" BINDIR="$build_dir" CC="$cc"
"$build_dir/ecm_pm1_test"

printf '%s\n' 'lasieve standalone tests passed'
