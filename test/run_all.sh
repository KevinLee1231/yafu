#!/bin/sh
# =============================================================================
# test/run_all.sh — 跑完所有回归，并对每一套的成败负责
#
# 存在的理由：这套回归以前是"谁跑谁负责"的——三个 shell 脚本各自 trap 清理
# 临时目录，顶层 Makefile 一条一行地调用。任何一套出问题，只要它的退出码没
# 传出来，整轮就是绿的。2026-10-07 在 yafu 上实测到：lasieve 那半边链接失败
# （tdsieve_sched2buf 没有实现），脚本照样报 0，`make test-standalone` 一直是
# 绿的。根因是以成功命令收尾的 EXIT trap 会把显式 exit 1 变成 0。
#
# 所以这里不信任任何单一脚本的退出码：逐套记录、逐套打印、最后汇总，
# 任何一套非 0 就整体非 0。
#
# 用法：
#   sh test/run_all.sh              # 全部
#   sh test/run_all.sh unit cli     # 只跑名字里含 unit / cli 的
#   YAFU=build/yafu sh test/run_all.sh
# =============================================================================
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root" || exit 1

YAFU=${YAFU:-build/yafu}
work=$(mktemp -d "${TMPDIR:-/tmp}/yafu-testrun.XXXXXX")

cleanup()
{
    rc=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$work"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# ---------------------------------------------------------------- 套件定义
# 名字 : 说明 : 命令。日志各自落在 $work/<名字>.log。
run_suite()
{
    _name=$1
    _desc=$2
    shift 2
    printf '  %-14s %-46s ' "$_name" "$_desc"
    if "$@" > "$work/$_name.log" 2>&1; then
        printf '通过\n'
        return 0
    fi
    _rc=$?
    printf '失败 (退出码 %d)\n' "$_rc"
    printf '      日志: %s\n' "$work/$_name.log"
    sed 's/^/        /' "$work/$_name.log" | tail -12
    return 1
}

suites_build()
{
    make -j"$(nproc 2>/dev/null || echo 4)" yafu
}

suites_unit()
{
    make -j"$(nproc 2>/dev/null || echo 4)" test
    ./build/yafu_test
}

suites_unit_fast()
{
    ./build/yafu_test --tag fast
}

suites_cli()
{
    sh test/test_cli.sh "$YAFU"
}

suites_nfs()
{
    CC=${CC:-cc} CXX=${CXX:-c++} sh test/test_nfs.sh
}

suites_lasieve()
{
    CC=${CC:-gcc} CXX=${CXX:-g++} sh test/test_lasieve.sh
}

suites_harness()
{
    # 自检：这套驱动自己必须能报出失败，否则上面那些"通过"也没有意义。
    sh test/harness_selftest.sh "$0"
}

ALL_NAMES="build unit unit_fast cli nfs lasieve harness"

# ---------------------------------------------------------------- 选择套件
if [ $# -gt 0 ]; then
    selected=""
    for pat in "$@"; do
        for n in $ALL_NAMES; do
            case "$n" in
                *"$pat"*) selected="$selected $n" ;;
            esac
        done
    done
    names=$(echo "$selected" | tr ' ' '\n' | awk 'NF' | sort -u | tr '\n' ' ')
    [ -n "$names" ] || { echo "没有匹配的套件：$*" >&2; exit 2; }
else
    names=$ALL_NAMES
fi

echo "=== yafu 回归 ==="
echo "  仓库    : $repo_root"
echo "  可执行  : $YAFU"
echo "  套件    : $names"
echo

failed=""
passed=""
total=0

for n in $names; do
    total=$((total + 1))
    case "$n" in
        build)     desc="编译 yafu"                        ; cmd=suites_build ;;
        unit)      desc="分层单元测试（算术/素性/ECM/选项）" ; cmd=suites_unit ;;
        unit_fast) desc="分层单元测试 --tag fast"           ; cmd=suites_unit_fast ;;
        cli)       desc="命令行 / 管道 / 批处理回归"         ; cmd=suites_cli ;;
        nfs)       desc="NFS 文件拆分与批量因子树（独立编译）"; cmd=suites_nfs ;;
        lasieve)   desc="筛法器独立回归（六个 I 值 + stat/test）"; cmd=suites_lasieve ;;
        harness)   desc="本驱动的自检（必须能报出失败）"      ; cmd=suites_harness ;;
        *)         desc="未知"                              ; cmd=true ;;
    esac
    if run_suite "$n" "$desc" "$cmd"; then
        passed="$passed $n"
    else
        failed="$failed $n"
    fi
done

echo
echo "=== 汇总 ==="
echo "  通过 : $passed"
if [ -n "$failed" ]; then
    echo "  失败 :$failed"
    echo "  完整日志：$work（本次运行结束后会清理，失败时请立刻重跑并保存）"
    # 保留失败日志，别在报错的同时把证据删掉
    for n in $failed; do
        [ -f "$work/$n.log" ] && cp "$work/$n.log" "./test-failed-$n.log"
    done
    exit 1
fi
echo "  全部 $total 套通过"
exit 0
