#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/yafu-nfs.XXXXXX")
trap 'rm -rf -- "$task_dir"' EXIT HUP INT TERM
mkdir -p "$task_dir/data"

cc=${CC:-cc}
cflags=${CFLAGS:--O2}
cppflags=${CPPFLAGS:-}

# 生产函数各自放入独立 section，链接时只保留驱动实际调用的路径。
common_flags="-UNDEBUG -std=c11 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVBITS=64 -DUSE_NFS -ffunction-sections -fdata-sections"
cd "$repo_dir"
includes="-I. -Iinclude -Ims_include -Itop -Itop/aprcl -Itop/cmdParser -Ifactor -Ifactor/gmp-ecm -Ifactor/nfs -Iytools -Iysieve -Ignfs -Ignfs/poly -Ignfs/poly/stage1"

# CFLAGS/CPPFLAGS 允许常规的空格分隔编译选项。
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes -c "$repo_dir/factor/nfs/nfs_poly.c" -o "$task_dir/nfs_poly.o"
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes -c "$repo_dir/factor/nfs/nfs_sieving.c" -o "$task_dir/nfs_sieving.o"
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes -c "$repo_dir/factor/nfs/nfs_filemanip.c" -o "$task_dir/nfs_filemanip.o"
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes -c "$repo_dir/factor/nfs/snfs.c" -o "$task_dir/snfs.o"
# shellcheck disable=SC2086
$cc $cppflags $cflags $common_flags $includes \
    "$repo_dir/test/standalone/nfs/nfs_review.c" \
    "$task_dir/nfs_poly.o" "$task_dir/nfs_sieving.o" \
    "$task_dir/nfs_filemanip.o" "$task_dir/snfs.o" \
    -Wl,--gc-sections -lgmp -lm -lpthread -o "$task_dir/nfs_review"
"$task_dir/nfs_review" "$task_dir/data"

# shellcheck disable=SC2086
$cc $cppflags $cflags -UNDEBUG -std=c11 -D_POSIX_C_SOURCE=200112L \
    -I"$repo_dir/include" -I"$repo_dir/ms_include" \
    "$repo_dir/test/standalone/nfs/ms_gmp_review.c" -lgmp \
    -o "$task_dir/ms_gmp_review"
"$task_dir/ms_gmp_review"
