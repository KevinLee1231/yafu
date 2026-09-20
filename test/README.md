# YAFU modular test system

Run the test targets from the repository root. They use the same compiler,
GMP paths, ISA selection and optional libraries as the main build, as
configured in `config.mk`.

```sh
make test                 # 构建测试框架与第 0-2 层测试
./yafu_test
make test-full            # 加入第 3 层测试
./yafu_test_full calc      # 运行计算器与因数分解对象的回归测试
./yafu_test_full --tag fast
```

`make test-run` and `make test-full-run` build and run their respective
executables. The latter includes the 50-, 60- and 70-digit SIQS cases, which
can take longer than the other tests. `make test-clean` removes the test
executables, test objects and the common test archive.

## Modules

| Layer | Modules | Checks |
| --- | --- | --- |
| Framework | `meta`, shared data self-check | Assertions, deterministic RNG and known-answer corpus |
| 0 | `mp_arith`, `mp_bitscan` | Platform arithmetic and bit operations |
| 1 | `sp_arith`, `modular`, `primality`, `sieve` | Arithmetic, modular operations, primality and prime generation |
| 2 | `microecm`, `tinyecm` | Small-factor routines |
| 3 | `siqs` | Complete factorizations of 50-, 60- and 70-digit inputs |
| 3 | `calc` | String/stack growth, long expressions, nested calls, optional argument scopes, unary operators, invalid-input recovery, assignment limits, totient values, and factor-object copying/lifetime |

Layer 3 links the factoring archives. Calculator tests also link the
frontend objects except `top/driver.o`, since the test runner supplies its
own `main()`. They exercise the parser in process; command-line terminal
and pipe handling still need separate checks.

## Running selected tests

```sh
./yafu_test_full --list
./yafu_test_full calc --tag calc-long
./yafu_test_full calc --tag calc-copy
./yafu_test_full mp_arith modular
./yafu_test_full --seed 12345
./yafu_test_full --stop
./yafu_test_full --bench
```

Bare arguments select modules. `--tag` selects tests by their listed tags;
`--bench` additionally runs optional timing measurements. Assertion
failures cause a nonzero exit status. Each test uses an independent RNG
stream derived from the printed seed.

For GCC or Clang builds with AddressSanitizer and UndefinedBehaviorSanitizer
available:

```sh
make test-calc-sanitize
```

This builds and runs the `calc` regressions with instrumentation in the
test sources, `top/cmdParser/calc.c` and `factor/factor_common.c`. Other
archives and frontend objects retain the normal build flags. The target
does not replace the normal objects or executables.

To add a module, define `tk_test` entries and a `tk_module` in a source file,
register it in `test_main.c`, and add it to the appropriate source list in
the root `Makefile`. Use independent expected results and include failure
paths as well as successful calculations.
