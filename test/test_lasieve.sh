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

compile -I. -Ifactor/lasieve5_64 -Ifactor/lasieve5_64/asm -Iinclude \
    test/standalone/lasieve/core_regression.c \
    factor/lasieve5_64/gmp-aux.c factor/lasieve5_64/redu2.c \
    factor/lasieve5_64/if.c -lgmp -lm -o "$build_dir/core_regression"
"$build_dir/core_regression"

compile -DNEED_ASPRINTF -I. -Ifactor/lasieve5_64 \
    -Ifactor/lasieve5_64/asm -Iinclude \
    test/standalone/lasieve/asprintf_regression.c \
    factor/lasieve5_64/if.c -lgmp -o "$build_dir/asprintf_regression"
"$build_dir/asprintf_regression"

compile -fsanitize=address -ffunction-sections -fdata-sections \
    -I. -Ifactor/lasieve5_64 -Ifactor/lasieve5_64/asm -Iinclude \
    -Ims_include -Iytools -Itop/aprcl \
    test/standalone/lasieve/batch_tree_regression.c \
    factor/lasieve5_64/if.c -Wl,--gc-sections -lgmp -lm \
    -o "$build_dir/batch_tree_regression"
ASAN_OPTIONS=detect_leaks=1 "$build_dir/batch_tree_regression"

compile -Wformat=2 -I. -Ifactor/lasieve5_64 \
    -Ifactor/lasieve5_64/asm -Iinclude \
    test/standalone/lasieve/input_poly_regression.c \
    factor/lasieve5_64/input-poly.c factor/lasieve5_64/if.c \
    -lgmp -o "$build_dir/input_poly_regression"
"$build_dir/input_poly_regression"

compile -ffunction-sections -fdata-sections -I. \
    -Ifactor/lasieve5_64 -Ifactor/lasieve5_64/asm -Iinclude -Iytools \
    test/standalone/lasieve/process_batch_helpers_regression.c \
    -Wl,--gc-sections -lgmp -o "$build_dir/process_batch_helpers_regression"
"$build_dir/process_batch_helpers_regression"

printf '%s\n' 'lasieve standalone tests passed'
