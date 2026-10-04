#!/bin/sh

# siever 的正确性基准。
#
# 用仓库自带的 SNFS 样例多项式跑一段固定的 special q 区间，校验产出的
# 关系集合逐字节一致。siever 是确定性的：同一组参数连跑三遍结果相同，
# 所以任何对筛法数值路径的改动——全局状态搬进 context 结构体、汇编
# 改传参——只要动了运算顺序或精度，这里就会炸。
#
# 三个容易踩的调用约定：
#   * 输入文件名是**位置参数**，不是 -i（-i 是首筛侧）
#   * 必须给 -a 或 -r 指定特殊 q 在哪一侧，否则只打印 usage
#   * -f 的起点必须落在一个真实大素数附近；-f 0 会让 side 1 的调度分配
#     直接 xmalloc: Cannot allocate memory
#
# 参数刻意取小：200 个 spq 约 15 秒、15 条关系，够跑进测试套件。

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work_dir=$(mktemp -d /tmp/yafu-sieve-oracle.XXXXXX)

cleanup()
{
    rm -rf "$work_dir"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

cd "$work_dir"

POLY="$repo_root/factor/nfs/lasieve/R942_poly.txt"
SIEVER=${1:-$repo_root/factor/nfs/lasieve/bin/local/gnfs-lasieve4I13e}

# 基准值：0.34257 之外这里用的是 R942 的 skew
SKew=0.63913
SPQ_START=650000
SPQ_COUNT=200
EXPECT_LINES=15
EXPECT_MD5=047c6a1d170dd2bc00e3daf99262a54c

if [ ! -x "$SIEVER" ]; then
    echo "siever 不存在或不可执行: $SIEVER" >&2
    echo "先跑 make lasieve" >&2
    exit 1
fi

cp "$POLY" ggnfs

"$SIEVER" -a -c "$SPQ_COUNT" -f "$SPQ_START" -S "$SKew" ggnfs >/dev/null 2>&1 || true

out="ggnfs.lasieve-0.$SPQ_START-$((SPQ_START + SPQ_COUNT))"

if [ ! -f "$out" ]; then
    echo "FAIL: siever 没有产出 $out" >&2
    exit 1
fi

lines=$(wc -l < "$out" | tr -d ' ')
if [ "$lines" != "$EXPECT_LINES" ]; then
    echo "FAIL: 关系条数 $lines，期望 $EXPECT_LINES" >&2
    exit 1
fi

md5=$(md5sum "$out" | cut -d' ' -f1)
if [ "$md5" != "$EXPECT_MD5" ]; then
    echo "FAIL: 关系集合变了，md5=$md5，期望 $EXPECT_MD5" >&2
    echo "      筛法数值路径被改动了，或者基准需要重新生成" >&2
    exit 1
fi

echo "sieve oracle: $lines relations, md5 $md5 -- ok"
