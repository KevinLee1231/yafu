#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/yafu-nfs.XXXXXX")
trap 'rm -rf -- "$task_dir"' EXIT HUP INT TERM
mkdir -p "$task_dir/data"

cc=${CC:-cc}
cflags=${CFLAGS:--O2}
cppflags=${CPPFLAGS:-}
# yafu 自身已经是 C++，这四个生产文件按 C++ 编；
# 驱动与生产文件同为 C++，两边都靠头里的声明对齐链接名。
cxx=${CXX:-g++}
cxxflags=${CXXFLAGS:--O2}

# 生产函数各自放入独立 section，链接时只保留驱动实际调用的路径。
common_flags="-UNDEBUG -std=c11 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVBITS=64 -DUSE_NFS -ffunction-sections -fdata-sections"
cxx_common_flags="-UNDEBUG -std=c++26 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVBITS=64 -DUSE_NFS -ffunction-sections -fdata-sections"
cd "$repo_dir"
includes="-I. -Ifactor/shared/include -Ifactor/shared/include -Itop -Ifactor/shared/aprcl/include -Itop/cmdParser -Ifactor -Ifactor/siqs/include -Ifactor/ecm/include -Ifactor/mpqs/include -Ifactor/nfs/gnfs/include -Ifactor/core/include -Ifactor/ecm -Ifactor/nfs -Ifactor/shared/ytools/include -Ifactor/shared/ysieve/include -Ifactor/shared/common/filter/include -Ifactor/shared/common/lanczos/include -Ifactor/nfs/gnfs -Ifactor/nfs/gnfs/poly -Ifactor/nfs/gnfs/poly/stage1 -Ifactor/nfs/gnfs/filter/include -Ifactor/nfs/gnfs/poly/include -Ifactor/nfs/gnfs/poly/stage1/include -Ifactor/nfs/gnfs/poly/stage1/stage1_core_gpu/include -Ifactor/nfs/gnfs/poly/stage2/include -Ifactor/nfs/gnfs/sieve/include -Ifactor/nfs/gnfs/sqrt/include"

# CFLAGS/CPPFLAGS 允许常规的空格分隔编译选项。
# shellcheck disable=SC2086
$cxx $cppflags $cxxflags $cxx_common_flags $includes -c "$repo_dir/factor/nfs/nfs_poly.cpp" -o "$task_dir/nfs_poly.o"
# shellcheck disable=SC2086
$cxx $cppflags $cxxflags $cxx_common_flags $includes -c "$repo_dir/factor/nfs/nfs_sieving.cpp" -o "$task_dir/nfs_sieving.o"
# shellcheck disable=SC2086
$cxx $cppflags $cxxflags $cxx_common_flags $includes -c "$repo_dir/factor/nfs/nfs_filemanip.cpp" -o "$task_dir/nfs_filemanip.o"
# shellcheck disable=SC2086
$cxx $cppflags $cxxflags $cxx_common_flags $includes -c "$repo_dir/factor/nfs/snfs.cpp" -o "$task_dir/snfs.o"
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes \
    "$repo_dir/test/standalone/nfs/nfs_review.cpp" \
    "$task_dir/nfs_poly.o" "$task_dir/nfs_sieving.o" \
    "$task_dir/nfs_filemanip.o" "$task_dir/snfs.o" \
    -Wl,--gc-sections -lgmp -lm -lpthread -o "$task_dir/nfs_review"
"$task_dir/nfs_review" "$task_dir/data"

# shellcheck disable=SC2086
$cxx $cppflags $cxxflags -UNDEBUG -std=c++26 -D_POSIX_C_SOURCE=200112L \
    -I"$repo_dir/factor/shared/include" \
    "$repo_dir/test/standalone/nfs/ms_gmp_review.cpp" -lgmp \
    -o "$task_dir/ms_gmp_review"
"$task_dir/ms_gmp_review"
