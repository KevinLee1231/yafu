#!/bin/sh

# siever 的正确性基准。
#
# 用仓库自带的 SNFS 样例多项式跑一段固定的 special q 区间，校验六个 I 值各自
# 产出的关系集合。siever 是确定性的：同一组参数连跑多遍结果相同，所以任何对
# 筛法数值路径的改动——全局状态搬进 context 结构体、汇编改传参、六个 I 值
# 合成一个二进制时的按 I 改名——只要动了运算顺序或精度，这里就会炸。
#
# 四个容易踩的调用约定：
#   * 六个 I 值是**一个**程序的六个入口，I 值是它的第一个参数：
#       <程序> <I> [选项...]
#     早期版本是六个可执行文件 gnfs-lasieve4I11e .. gnfs-lasieve4I16e；再早一步
#     是 yafu 自己带一个 gnfs-lasieve4e 子进程。筛法器现在是 yafu 的一部分，
#     由 lasieve_run() 在进程内调用，make all 只产出 yafu 一个可执行文件；
#     测试要单独跑筛法器，所以用 lasieve/Makefile 的 check_sieve 目标现编一个。
#   * 输入文件名是**位置参数**，不是 -i（-i 是首筛侧）
#   * 必须给 -a 或 -r 指定特殊 q 在哪一侧，否则只打印 usage
#   * -f 的起点必须落在一个真实大素数附近；-f 0 会让 side 1 的调度分配
#     直接 xmalloc: Cannot allocate memory
#
# 比对的是"规范指纹"而不是文件本身的 md5：把每行里的所有数字排序，再把全部
# 行排序，最后取 md5。这样同一对大基谁写在前面、哪一侧在前、行的先后都不影
# 响结果，而任何一个 q、大基或小素数变了指纹一定变。
#
# 换成这个口径是有原因的：合成单一二进制之后，关系集合与原来逐条相同，但文件
# 里的书写顺序变了（同一对大基的先后、行与行的先后），原始 md5 六个 I 值全部
# 改变。指纹既能守住数值路径，又不会被这种无害的顺序变化误报。下面这组期望值
# 取自合成之前的六个可执行文件，所以这项基准仍然在守着合成前后的等价性。
#
# 耗时约 5 分钟（实测 4m46s），六�� I 值每个都要重新准备因子基，
# 每次约 30 秒，缩小区间宽度省不下来：I=11 在 60 个 spq 和 200 个 spq 上
# 关系数都是 6 条、耗时都是 35 秒。之前这项只查 I=13 一个值，现在六个都查，
# 因为合成的目的就是让六个 I 值共处一份映像。

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

POLY="$repo_root/factor/nfs/lasieve/R942_poly.txt"
# 筛法器驱动由调用者给出（test_lasieve.sh 用 make -C factor/nfs/lasieve 现编的
# check_sieve）。没有它就直接跑 yafu 是没意义的：yafu 是分解器，不是筛法器。
SIEVER=${1:-}
if [ -z "$SIEVER" ] || [ ! -x "$SIEVER" ]; then
    echo "no siever driver given or not executable: $SIEVER" >&2
    echo "用法: sh sieve_oracle.sh <check_sieve 的路径>" >&2
    echo "test_lasieve.sh 会自己编一个" >&2
    exit 1
fi

# 基准值：0.34257 之外这里用的是 R942 的 skew
Skew=0.63913
SPQ_START=650000
SPQ_COUNT=200

# I 值  条数  规范指纹（取自合成之前的六个可执行文件）
EXPECTED_11='6 c9474542c12bda6470e8494cde713dd3'
EXPECTED_12='6 1e214830ae5f99ae4c16a58d613669ce'
EXPECTED_13='15 d33154ebddae8006c1c788c4298e9cab'
EXPECTED_14='28 f9f1cb1724a991c06e2012f331f7247d'
EXPECTED_15='51 d5e527993fd9fe389b66288577d637ad'
EXPECTED_16='89 c634dc3f781fe8fc58fa081b4fb84fc6'

# 每行所有数字排序，再把所有行排序，最后 md5
fingerprint()
{
    awk -F'[:,]' '{
        n = NF
        for (i = 1; i <= n; i++) a[i] = $i
        for (i = 2; i <= n; i++) {
            t = a[i]; j = i - 1
            while (j >= 1 && a[j] > t) { a[j+1] = a[j]; j-- }
            a[j+1] = t
        }
        s = a[1]
        for (i = 2; i <= n; i++) s = s "," a[i]
        print s
    }' "$1" | sort | md5sum | cut -d' ' -f1
}

fail=0
for I in 11 12 13 14 15 16; do
    eval "want=\$EXPECTED_$I"
    want_lines=${want%% *}
    want_md5=${want##* }

    dir="$work_dir/i$I"
    mkdir -p "$dir"
    cp "$POLY" "$dir/ggnfs"
    ( cd "$dir" && "$SIEVER" "$I" -a -c "$SPQ_COUNT" -f "$SPQ_START" -S "$Skew" ggnfs \
        >/dev/null 2>&1 || true )

    out=$(ls "$dir"/ggnfs.lasieve-* 2>/dev/null | head -1 || true)
    if [ -z "$out" ] || [ ! -f "$out" ]; then
        echo "FAIL: I=$I siever 没有产出关系文件" >&2
        fail=$((fail + 1))
        continue
    fi

    lines=$(wc -l < "$out" | tr -d ' ')
    if [ "$lines" != "$want_lines" ]; then
        echo "FAIL: I=$I 关系条数 $lines，期望 $want_lines" >&2
        fail=$((fail + 1))
        continue
    fi

    md5=$(fingerprint "$out")
    if [ "$md5" != "$want_md5" ]; then
        echo "FAIL: I=$I 关系集合变了，指纹 $md5，期望 $want_md5" >&2
        echo "      筛法数值路径被改动了，或者基准需要重新生成" >&2
        fail=$((fail + 1))
        continue
    fi

    echo "  I=$I  $lines relations, 指纹 $md5 -- ok"
done

if [ "$fail" -ne 0 ]; then
    echo "sieve oracle: $fail 个 I 值不合格" >&2
    exit 1
fi

echo "sieve oracle: 六个 I 值全部与基准一致 -- ok"
