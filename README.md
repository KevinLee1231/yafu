# YAFU — Yet Another Factoring Utility

Automated integer factorization. 

YAFU (with assistance from other free software) uses the most powerful modern algorithms (and implementations of them) to factor input integers in a completely automated way.  

YAFU has been referenced several times in the academic literature.  If you have academic work that requires integer factorization, YAFU might be able to help.  
 
For factorization help, support, and discussion, see the community at <https://www.mersenneforum.org/node/58>.

> **Detailed documentation lives in the [project wiki](../../wiki).** The wiki covers every option, every callable function, and a fuller version of the build instructions below.

---

## Quick start





### First run

```
yafu "factor(rsa(200))"
```

Or launch the interactive prompt:

```
yafu
>> factor(2056802480868100646375721251575555494408897387375737955882170045672576386016591560879707933101909539325829251496440620798637813)
```

YAFU reads `yafu.ini` from, and writes its logs and savefiles to, the **current working directory** — not the directory holding the executable. On Windows the two are usually the same, since you tend to run YAFU from its own folder; on Linux they differ, so `cd` into the directory containing your `yafu.ini` before running `yafu`. No installation step is needed.

---

## Building from source

Pre-built Windows binaries are provided, but **building yourself, tuned to your CPU, will very likely be faster.** The whole build is driven by a `Makefile`, configured per-machine through `config.mk`.

### Dependencies

| Required | Optional |
| --- | --- |
| **GMP** — <https://gmplib.org/> | **GMP-ECM** — <http://ecm.gforge.inria.fr/> (for ECM factorization) |
| | **CUDA Toolkit** (for GPU cofactorization / poly select / LA) |
| | **OpenCL** (alternative to CUDA for cofactorization) |

As of YAFU 3.0, `ytools`, `ysieve`, and `msieve` are bundled in this repository and built as part of the YAFU build.

### Linux, WSL, and MSYS2/MinGW-w64

在 WSL Kali 中，从源码目录运行下列命令即可；无需安装到系统目录。已有
`config.mk` 时保留本机配置。`make info` 会显示实际使用的 GMP、GMP-ECM
路径和 CPU 选项；显式设置 `ECM=1` 时如果缺少头文件，构建会报错。

```sh
make -j4 yafu
./yafu 'factor(91)' -terse
make -j4 lasieve
make -j4 test-full
./yafu_test_full --tag fast
make test-cli
```

`USE_NATIVE=1` 根据当前 WSL 可见的 CPU 编译；`FORCE_GENERIC=1` 关闭 CPU
扩展选项，适合检查普通 x86-64 路径。更改编译器或编译选项会自动重建对象，
不需要手动删除旧对象。完整检查入口见 [测试说明](test/README.md)。

```bash
cp config.mk.example config.mk          # first time only — edit paths as needed
make yafu                               # builds with sensible defaults (gcc)
```

Pass feature/ISA flags either on the command line or by uncommenting them in `config.mk`. The most useful ones are listed below. For the complete set, see the [Building YAFU](../../wiki/Building-YAFU) wiki page or run `make help`.

```bash
# Inspect the resolved configuration before/during a build
make info
make help

# Using config.mk for configuration of ISA and features
make yafu

or

make all

# ISA selection — overriding config.mk with specified ISA
make yafu USE_AVX2=1
make yafu USE_AVX512=1          # Skylake-X, Cascade Lake, Zen 4, etc.
make yafu USE_AVX512IFMA=1      # Ice Lake and newer

# ISA selection — overriding config.mk with feature flags or compiler options
make yafu ECM=1 OMP=1           # link GMP-ECM, enable OpenMP
make yafu CC=clang              # use clang
make yafu DEBUG=1               # debug build

```

The first time you set up, copy `config.mk.example` to `config.mk` and edit any paths that differ from your system. `config.mk` is gitignored — only `config.mk.example` is tracked.

### Other useful targets

| Target | Builds |
| --- | --- |
| `yafu` | The main yafu executable |
| `msieve` | The msieve static library + demo |
| `all` | Both of the above |
| `info` | Print the fully resolved configuration |
| `help` | Show the feature-flag reference |
| `clean` | Remove build artifacts |



---

## Continuous integration & release builds

This fork is Linux-only, so the **automated GitHub Actions pipeline** builds and tests on Linux alone:

- **Linux / gcc**, via `Makefile`, with `USER_LDFLAGS=-static` for a self-contained binary

For every push the CI builds the full matrix of ISA targets (`generic`, `sse41`, `avx2`, `avx512`, `avx512ifma`), runs the CLI regression script, and builds and runs the layered test binary. On tagged releases the resulting binary is attached to the release page.

See the [Continuous Integration](../../wiki/Continuous-Integration) wiki page for details on the matrix, the artifacts, and how the same `Makefile` flags drive both CI and local builds.

---

## GGNFS sievers (required for NFS)

For NFS factorizations, YAFU needs external GGNFS lattice sieve binaries (`ggnfs-lasieve4I*`). Linux and MinGW binaries are bundled under `factor/lasieve5_64/bin/`. Point YAFU at them with `ggnfs_dir=` in `yafu.ini`, or `-ggnfs_dir <path>` on the command line. Without these, NFS will not run.

WSL 源码构建使用 `make -j4 lasieve`，输出位于 `factor/lasieve5_64/bin/local/`。
仓库内的 `yafu.ini` 指向这个目录，以便 NFS 使用当前源码生成的筛选器。
这些本地产物不提交到 Git；已有预编译文件仍可通过 `-ggnfs_dir` 显式选择。

If you have AVX-512 on your CPU, YAFU will also use **AVX-ECM** as the default ECM backend (built in). A standalone version lives at <https://github.com/bbuhrow/avx-ecm>.

---

## Help and documentation

| Where | What |
| --- | --- |
| `help` at the YAFU prompt | Built-in help (reads `docfile.txt`) |
| `help <function>` | Per-function detail |
| `docfile.txt` | Function reference (must sit next to the binary for `help` to work) |
| `yafu.ini` | Every option, as commented lines |
| [Project wiki](../../wiki) | All of the above, cross-referenced and searchable |
| <https://www.mersenneforum.org/node/58> | Community / support |

---

## Fun examples

```
# Exercises many of yafu's algorithms
yafu "factor(2056802480868100646375721251575555494408897387375737955882170045672576386016591560879707933101909539325829251496440620798637813)"

# Neat example involving ecm and siqs
yafu "factor(140870298550359924914704160737419905257747544866892632000062896476968602578482966342704)"
```

If you build YAFU on a new platform or with a new compiler — or build a binary that beats one of the pre-compiled releases for a specific CPU — the maintainer would like to hear about it.
