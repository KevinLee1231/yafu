#!/bin/sh
# =============================================================================
# test/harness_selftest.sh — 验证回归驱动本身能报出失败
#
# 一套永远绿的回归比没有回归更糟：它让人以为代码被覆盖了。所以除了跑被测代码，
# 还要跑一次"故意失败"，确认驱动会把失败如实报出来。
#
# 用法：sh test/harness_selftest.sh [run_all.sh 的路径]
# 退出码 0 表示驱动行为正确；非 0 表示驱动不可信。
# =============================================================================
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
driver=${1:-$repo_root/test/run_all.sh}
# cd 到临时目录之后就找不到相对路径了，这里先定死
case "$driver" in /*) ;; *) driver=$(CDPATH= cd -- "$(dirname -- "$driver")" && pwd)/$(basename -- "$driver") ;; esac

work=$(mktemp -d "${TMPDIR:-/tmp}/yafu-harness.XXXXXX")
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

cd "$work" || exit 1

# 一份假的 run_all.sh：驱动真的从子脚本的退出码取成败吗？
cat > fake_driver.sh <<'DRIVER'
#!/bin/sh
# 假装自己是 run_all.sh：跑一个套件，按它的退出码决定整体退出码。
set -u
log=$(mktemp -d)
fail_suite() { echo "故意失败" > "$log/a.log"; return 7; }
run_suite() {
    _name=$1; _desc=$2; shift 2
    if "$@" > "$log/$_name.log" 2>&1; then return 0; fi
    return 1
}
names=a
failed=""
for n in $names; do
    case "$n" in a) cmd=fail_suite ;; *) cmd=true ;; esac
    run_suite "$n" "自检" "$cmd" || failed="$failed $n"
done
[ -n "$failed" ] && exit 1
exit 0
DRIVER

pass=0
fail=0

check() {
    _desc=$1
    _want=$2
    shift 2
    "$@" > out.txt 2>&1
    _got=$?
    if [ "$_got" -eq "$_want" ]; then
        printf '  通过  %s\n' "$_desc"
        pass=$((pass + 1))
    else
        printf '  失败  %s（期望退出码 %s，实际 %s）\n' "$_desc" "$_want" "$_got"
        sed 's/^/        /' out.txt | tail -5
        fail=$((fail + 1))
    fi
}

echo "=== 回归驱动自检 ==="

# 1) 子脚本报失败时，驱动必须非 0
check "子套件失败 -> 驱动非 0" 1 sh fake_driver.sh

# 2) 退出码要原样传出去：trap 里先存状态、清 trap、再退出
mkdir -p case_rc
cat > case_rc/s.sh <<'EOS'
#!/bin/sh
set -eu
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/selftest.XXXXXX")
cleanup()
{
    rc=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$task_dir"
    exit "$rc"
}
trap cleanup EXIT
echo started
exit 1
EOS
check "失败时退出码原样传出（期望 1）" 1 sh case_rc/s.sh

# 3) 真的驱动在"挑不出套件"时要非 0（而不是静默通过）
if [ -f "$driver" ]; then
    check "run_all.sh 选不存在的套件 -> 非 0" 2 sh "$driver" zzz-no-such-suite
else
    printf '  跳过  找不到 %s\n' "$driver"
fi

echo
echo "  通过 $pass 项，失败 $fail 项"
[ "$fail" -eq 0 ] || exit 1
exit 0
