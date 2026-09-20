#!/bin/sh
# 在独立目录运行真实入口，核对输出、错误状态和批处理文件。
set -eu
binary=${1:-./yafu}
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/yafu-cli.XXXXXX")
trap 'rm -rf -- "$task_dir"' EXIT HUP INT TERM
cd "$task_dir"
checks=0

run() {
    timeout 15 "$binary" "$@" -silent >output 2>error
}
has_line() {
    if ! grep -Fxq "$1" output; then
        cat output error
        printf 'missing output: %s\n' "$1" >&2
        exit 1
    fi
    checks=$((checks + 1))
}
reject() {
    if run "$@"; then
        printf 'unexpected success: %s\n' "$*" >&2
        exit 1
    else
        status=$?
    fi
    if [ "$status" -ne 1 ]; then
        cat output error
        printf 'unexpected failure status: %s\n' "$status" >&2
        exit 1
    fi
    checks=$((checks + 1))
}

run 'expr(6*7)' </dev/null
has_line 42
run -e 'expr(6*7)' </dev/null
has_line 42
set -- 'expr(6*7)'
i=0
while [ "$i" -lt 140 ]; do
    set -- "$@" -silent
    i=$((i + 1))
done
run "$@" </dev/null
has_line 42
run 91 -terse </dev/null
has_line '91=7*13'
printf 'expr(6*7)' | run
has_line 42
printf '21' | run 'expr(@+@)'
has_line 42
awk 'BEGIN { printf "expr(1"; for (i=1;i<600;i++) printf "+1"; printf ")" }' >long-expression
run "$(cat long-expression)" </dev/null
has_line 600
cat long-expression | run
has_line 600
reject 'expr(1' </dev/null
reject 'expr(1))' </dev/null
reject 'expr(1/0)' </dev/null
reject -threads 0 </dev/null
reject -threads 4294967296 </dev/null
reject -B1ecm 1e99999 </dev/null
reject -B1ecm 1e-1 </dev/null
reject -work nan </dev/null
reject -stopbase 1 </dev/null
reject -siqsSSalloc 16 </dev/null
reject -threads </dev/null
reject -unknown </dev/null
long_arg=$(awk 'BEGIN { for(i=0;i<256;i++) printf "x" }')
reject -script "$long_arg" </dev/null

printf 'expr(6*7)\nexpr(40+3)' >'input script'
run -script 'input script' -repeat 1 </dev/null
has_line 42
has_line 43
test "$(grep -cx 42 output)" -eq 2
test "$(grep -cx 43 output)" -eq 2
checks=$((checks + 2))
mkdir inputs
printf '21\n22' >inputs/batch
run 'expr(@+@)' -batchfile inputs/batch </dev/null
has_line 42
has_line 44
test ! -s inputs/batch
test ! -e inputs/batch.yafu-tmp
checks=$((checks + 2))
printf '21\n22' >inputs/batch
cp inputs/batch inputs/saved
touch inputs/batch.yafu-tmp
reject 'expr(@+@)' -batchfile inputs/batch </dev/null
cmp inputs/batch inputs/saved
checks=$((checks + 1))
printf 'threads\n' >yafu.ini
reject 'expr(6*7)' </dev/null
printf '  %% comment\nthreads = 2\nlogfile=\nB1ecm=1e6\njsonlog=a=b.json' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
printf 'tune_info=malformed\n' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
grep -q 'ignoring malformed tune_info' error
checks=$((checks + 1))
printf 'tune_info=legacy CPU,LINUX64,1,2,3,4,5,42\n' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
test ! -s error
checks=$((checks + 1))
printf 'tune_info=current CPU,LINUX64,1,2,3,4,5,0,0,0,42\n' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
test ! -s error
checks=$((checks + 1))
printf 'tune_info=partial CPU,LINUX64,1,2,3,4,5,6,42\n' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
grep -q 'ignoring malformed tune_info' error
checks=$((checks + 1))
printf 'tune_info=invalid CPU,LINUX64,1,2,3,4,5,nan,0,0,42\n' >yafu.ini
run 'expr(6*7)' </dev/null
has_line 42
grep -q 'ignoring non-finite tune_info' error
checks=$((checks + 1))
printf 'CLI checks: %s passed\n' "$checks"
