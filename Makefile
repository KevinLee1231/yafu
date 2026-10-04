# =============================================================================
# yafu — Unified Makefile
# Author of original Makefiles: Ben Buhrow
#
# Platforms  : Linux, Windows (MSYS2/MinGW-w64)
# Compilers  : gcc, clang, icc/icx   — select with  make CC=gcc|clang|icc
# Build type : release (default)     — debug with    make DEBUG=1
#
# Quick-start
# -----------
#   cp config.mk.example config.mk   # first time only; edit paths as needed
#   Add to .gitignore: config.mk  .deps/
#   make yafu                        # build yafu (default compiler: gcc)
#   make all                         # build yafu
#   make lasieve                     # build the external lattice sievers
#   make CC=clang yafu               # use clang
#   make CC=icc yafu                 # use Intel compiler
#   make DEBUG=1 yafu                # debug build
#   make info                        # show fully resolved configuration
#   make help                        # show feature-flag reference
#   make clean                       # remove all build artefacts
#
# Feature flags (pass on command line or set in config.mk):
#   OMP=1          OpenMP threading
#   ECM=1          Link GMP-ECM, enable ECM factorisation
#   BATCH_CUDA=1   GPU cofactorisation for NFS/SIQS  (requires CUDA)
#   CUDA_POLY=1    GPU NFS polynomial selection       (requires CUDA)
#   MPI=1          MPI parallel processing
#   VBITS=N        Linear algebra vector width: 64 (default), 128, 256
#   USE_SSE41=1      SSE 4.1
#   USE_AVX2=1       AVX2  (implies SSE 4.1)
#   USE_BMI2=1       BMI / BMI2
#   USE_AVX512=1     AVX-512 F/BW  (implies AVX2, BMI2; works on Skylake-X,
#                    Ice Lake, Zen 4, and any other AVX-512-capable CPU)
#   USE_AVX512IFMA=1 AVX-512 IFMA  (implies USE_AVX512; for Ice Lake and later)
#   USE_AVX512PF=1   AVX-512 PF    (implies USE_AVX512; for Xeon Phi / KNL)
#   USE_NATIVE=1     Auto-detect ISA from the build CPU and add -march=native.
#                    Works with gcc, clang, and icc.  Not suitable for
#                    cross-compilation or CI where reproducibility matters.
#                    Not supported on Windows/MinGW — use explicit ISA flags.
#   SMALLINT=1     Small SIQS intervals
#   PROFILE=1      gprof profiling
#   OPT_DEBUG=1    Optimisation debug output
#   TIMING=1       QS timing instrumentation
#   FORCE_GENERIC=1  Disable all SIMD paths
#   STATIC=1       Static link (experimental, Linux)
#   STATIC_WIN=1   Fully static Windows binary (runs in PowerShell/cmd without DLLs)
#   MINGW=1        Building under MinGW (skip -ldl)
# =============================================================================


# -----------------------------------------------------------------------------
# 1. LOAD USER CONFIG  (silently skipped if config.mk is absent)
# -----------------------------------------------------------------------------
-include config.mk


# -----------------------------------------------------------------------------
# 2. OS DETECTION
# -----------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    EXE_EXT     := .exe
else
    DETECTED_OS := $(shell uname -s)
    EXE_EXT     :=
endif

MKDIR  := mkdir -p
RM_RF  := rm -rf


# -----------------------------------------------------------------------------
# 3. COMPILER FAMILY DETECTION
# -----------------------------------------------------------------------------
CC ?= gcc
CC_BASENAME := $(notdir $(CC))

ifneq (,$(findstring icx,$(CC_BASENAME)))
    COMPILER_FAMILY := icc
else ifneq (,$(findstring icc,$(CC_BASENAME)))
    COMPILER_FAMILY := icc
else ifneq (,$(findstring clang,$(CC_BASENAME)))
    COMPILER_FAMILY := clang
else
    COMPILER_FAMILY := gcc
endif

# MPI overrides the compiler wrapper
ifeq ($(MPI),1)
    CC = mpicc
endif


# -----------------------------------------------------------------------------
# 4. DEPENDENCY DISCOVERY
#
#    Helper: search a list of candidate directories for a filename.
#    Returns the first directory where the file is found, or empty.
# -----------------------------------------------------------------------------
define find_dir
$(firstword $(foreach d,$(2),$(if $(wildcard $(d)/$(1)),$(d))))
endef

ifeq ($(DETECTED_OS),Windows)
    SYS_INC_PATHS := /mingw64/include /mingw32/include /usr/include
    SYS_LIB_PATHS := /mingw64/lib     /mingw32/lib     /usr/lib
else
    MULTIARCH := $(shell $(CC) -print-multiarch 2>/dev/null)
    SYS_INC_PATHS := $(if $(MULTIARCH),/usr/include/$(MULTIARCH)) \
                     /usr/include /usr/local/include /opt/local/include
    SYS_LIB_PATHS := /usr/lib /usr/lib64 /usr/local/lib \
                     $(if $(MULTIARCH),/usr/lib/$(MULTIARCH))
endif


# ---- 4a. GMP  (required) ---------------------------------------------------
ifdef GMP_PREFIX
    GMP_INCDIR ?= $(GMP_PREFIX)/include
    GMP_LIBDIR ?= $(GMP_PREFIX)/lib
endif
GMP_INCDIR ?= $(call find_dir,gmp.h,$(SYS_INC_PATHS))
GMP_LIBDIR ?= $(call find_dir,libgmp.a,$(SYS_LIB_PATHS))

ifeq (,$(wildcard $(GMP_INCDIR)/gmp.h))
    $(error GMP is required but gmp.h was not found. \
Set GMP_PREFIX or GMP_INCDIR in config.mk or on the command line.)
endif

# Normalise to absolute paths so sub-makes at deeper directory levels
# (factor/nfs/lasieve/ and factor/nfs/lasieve/asm/) receive correct paths
# even when config.mk used a relative value like ../gmp-install/mingw.
GMP_INCDIR := $(abspath $(GMP_INCDIR))
GMP_LIBDIR := $(abspath $(GMP_LIBDIR))

GMP_INC   := -I$(GMP_INCDIR)
GMP_LPATH := $(if $(GMP_LIBDIR),-L$(GMP_LIBDIR))


# ---- 4b. GMP-ECM  (required) -----------------------------------------------
ifdef ECM_PREFIX
    ECM_INCDIR ?= $(ECM_PREFIX)/include
    ECM_LIBDIR ?= $(ECM_PREFIX)/lib
endif
ECM_INCDIR ?= $(call find_dir,ecm.h,$(SYS_INC_PATHS))
 
# Normalise to absolute paths before the header check — same reason as GMP:
# relative paths in config.mk must not propagate into linker flags or
# sub-makes.  Applied unconditionally so the wildcard check below uses
# unambiguous absolute paths.
ifneq (,$(ECM_INCDIR))
    ECM_INCDIR := $(abspath $(ECM_INCDIR))
endif
ifneq (,$(ECM_LIBDIR))
    ECM_LIBDIR := $(abspath $(ECM_LIBDIR))
endif
 
ifneq (,$(wildcard $(ECM_INCDIR)/ecm.h))
    HAVE_ECM_LIB := 1
    ECM_INC      := -I$(ECM_INCDIR)
    ECM_LPATH    := $(if $(ECM_LIBDIR),-L$(ECM_LIBDIR))
else
    HAVE_ECM_LIB :=
    ECM_INC      :=
    ECM_LPATH    :=
endif


# ---- 4c. CUDA Toolkit  (optional) ------------------------------------------
# Discovery precedence (first match wins):
#   1. CUDA_PREFIX set in config.mk or on the command line
#   2. CUDA_ROOT environment variable  (set by the NVIDIA installer on Linux)
#   3. CUDA_PATH environment variable  (set by the NVIDIA installer on Windows)
#   4. nvcc found on PATH  (derive root from its location)
#   5. Well-known filesystem locations  (/usr/local/cuda, /usr/local/cuda-12 …)
#
# Steps 2-5 are skipped entirely when CUDA_PREFIX is already defined,
# so config.mk / command-line always wins without any ambiguity.
ifndef CUDA_PREFIX
    # 2. CUDA_ROOT (Linux installer standard)
    ifneq (,$(CUDA_ROOT))
        CUDA_PREFIX := $(CUDA_ROOT)
    # 3. CUDA_PATH (Windows installer standard)
    else ifneq (,$(CUDA_PATH))
        CUDA_PREFIX := $(CUDA_PATH)
    # 4. nvcc on PATH
    else
        _NVCC_ON_PATH := $(shell which nvcc 2>/dev/null)
        ifneq (,$(_NVCC_ON_PATH))
            CUDA_PREFIX := $(shell dirname $(_NVCC_ON_PATH))/..
        # 5. Common filesystem locations
        else
            CUDA_PREFIX := $(firstword $(wildcard \
                /usr/local/cuda \
                /usr/local/cuda-12 \
                /usr/local/cuda-11))
        endif
    endif
endif

ifdef CUDA_PREFIX
    CUDA_INCDIR ?= $(CUDA_PREFIX)/include
    ifeq ($(DETECTED_OS),Windows)
        CUDA_LIBDIR ?= $(CUDA_PREFIX)/lib/x64
    else
        CUDA_LIBDIR ?= $(CUDA_PREFIX)/lib64
    endif
    NVCC        ?= $(CUDA_PREFIX)/bin/nvcc
endif

ifneq (,$(wildcard $(CUDA_INCDIR)/cuda.h))
    HAVE_CUDA_TOOLKIT := 1
    CUDA_INC          := -I$(CUDA_INCDIR)
    CUDA_LPATH        := $(if $(CUDA_LIBDIR),-L$(CUDA_LIBDIR))
else
    HAVE_CUDA_TOOLKIT :=
    CUDA_INC          :=
    CUDA_LPATH        :=
    NVCC              :=
    # If user requested CUDA features but toolkit not found, warn and disable
    ifdef BATCH_CUDA
        $(warning CUDA toolkit not found — disabling BATCH_CUDA)
        override BATCH_CUDA :=
    endif
    ifdef CUDA_POLY
        $(warning CUDA toolkit not found — disabling CUDA_POLY)
        override CUDA_POLY :=
    endif
    ifdef CUDA_LA
        $(warning CUDA toolkit not found — disabling CUDA_LA)
        override CUDA_LA :=
    endif
endif


# ---- 4d. OpenCL  (optional, only meaningful with BATCH_CUDA) ---------------
# OpenCL is used as an alternative to CUDA for GPU cofactorisation on AMD
# and other non-NVIDIA hardware.  There is no point probing for it or linking
# it when BATCH_CUDA is not enabled.
ifdef BATCH_CUDA
ifdef OCL_PREFIX
    OCL_INCDIR ?= $(OCL_PREFIX)/include
    OCL_LIBDIR ?= $(OCL_PREFIX)/lib
endif
OCL_INCDIR ?= $(call find_dir,CL/cl.h,$(SYS_INC_PATHS))

# Check for the library as well as the header — on some systems (e.g. MinGW)
# the headers are installed without a matching ICD loader library.
ifeq ($(DETECTED_OS),Windows)
    _OCL_LIBNAMES := libOpenCL.a libOpenCL.dll.a OpenCL.lib
else
    _OCL_LIBNAMES := libOpenCL.so libOpenCL.a
endif

OCL_LIBDIR ?= $(firstword $(foreach l,$(_OCL_LIBNAMES),\
                  $(call find_dir,$(l),$(SYS_LIB_PATHS))))

_OCL_HEADER_FOUND := $(wildcard $(OCL_INCDIR)/CL/cl.h)
_OCL_LIB_FOUND    := $(if $(OCL_LIBDIR),$(firstword $(foreach l,$(_OCL_LIBNAMES),\
                         $(wildcard $(OCL_LIBDIR)/$(l)))))

ifneq (,$(_OCL_HEADER_FOUND))
ifneq (,$(_OCL_LIB_FOUND))
    HAVE_OPENCL := 1
    OCL_INC     := -I$(OCL_INCDIR)
    OCL_LPATH   := -L$(OCL_LIBDIR)
    OCL_LIBS    := -lOpenCL
else
    HAVE_OPENCL :=
    OCL_INC     :=
    OCL_LPATH   :=
    OCL_LIBS    :=
    $(info OpenCL: header found ($(OCL_INCDIR)) but no library — disabling. \
        Set OCL_PREFIX in config.mk if you have it installed.)
endif
else
    HAVE_OPENCL :=
    OCL_INC     :=
    OCL_LPATH   :=
    OCL_LIBS    :=
endif
else
    # BATCH_CUDA not set — skip OpenCL entirely
    HAVE_OPENCL :=
    OCL_INC     :=
    OCL_LPATH   :=
    OCL_LIBS    :=
endif


# -----------------------------------------------------------------------------
# 5. CUDA SM (streaming multiprocessor / compute capability) version
#
#    SM controls the -arch sm_$(SM) flag passed to nvcc for .ptx compilation.
#
#    Precedence (first match wins — auto-detect is skipped if SM is already set):
#      1. SM=XY on the command line or in config.mk
#      2. A numeric value passed via BATCH_CUDA or CUDA_POLY (legacy behaviour)
#      3. Auto-detected from nvidia-smi (lowest capability across all cards,
#         so the .ptx runs correctly on every GPU in a multi-card system)
#      4. Hardcoded fallback of 80 (Ampere) with a warning
#
#    To override: set SM := 89 in config.mk, or  make SM=89
# -----------------------------------------------------------------------------

# Legacy: BATCH_CUDA=XY or CUDA_POLY=XY used to encode the SM value directly.
# Honour that for backward compatibility, but only if SM isn't already set.
ifndef SM
    ifdef BATCH_CUDA
        ifneq ($(BATCH_CUDA),1)
            SM         := $(BATCH_CUDA)
            BATCH_CUDA := 1
        endif
    endif
endif

ifndef SM
    ifdef CUDA_POLY
        ifneq ($(CUDA_POLY),1)
            SM        := $(CUDA_POLY)
            CUDA_POLY := 1
        endif
    endif
endif

# Normalize BATCH_CUDA / CUDA_POLY to plain 1 if they held a numeric SM value
# (which has already been captured into SM above).
ifneq (,$(BATCH_CUDA))
    BATCH_CUDA := 1
endif
ifneq (,$(CUDA_POLY))
    CUDA_POLY := 1
endif

# Auto-detect SM from nvidia-smi when CUDA is present and SM is still unset.
ifndef SM
    ifdef HAVE_CUDA_TOOLKIT
        # Query compute capability for every GPU, strip the dot (8.0 → 80),
        # sort numerically, and take the lowest value so the .ptx is
        # compatible with every card in the system.
        _SMS_RAW := $(shell nvidia-smi --query-gpu=compute_cap \
                        --format=csv,noheader 2>/dev/null | tr -d '.' | sort -n)
        ifneq (,$(_SMS_RAW))
            SM := $(firstword $(_SMS_RAW))
            $(info CUDA: auto-detected SM=$(SM) \
                (lowest capability across $(words $(_SMS_RAW)) GPU(s): $(_SMS_RAW)))
        else
            SM := 80
            $(warning CUDA: nvidia-smi found no GPUs — falling back to SM=$(SM). \
                Set SM in config.mk if this is wrong.)
        endif
    else
        # CUDA toolkit not present; SM won't be used, but give it a value
        # so any $(SM) references in non-CUDA rules don't expand to empty.
        SM := 80
    endif
endif


# -----------------------------------------------------------------------------
# 6. BASE CFLAGS
# -----------------------------------------------------------------------------
CFLAGS := \
    -fno-common \
    -m64 \
	-g \
    -std=gnu11 \
    -fPIE \
    -DUSE_NFS \
    -D_FILE_OFFSET_BITS=64 \
    -D_LARGEFILE64_SOURCE \
	-DHAVE_CPU_HASHTABLE \
    -Wall \
    -Wconversion

# GCC 12+ strict aliasing is more aggressive than GCC 11 and triggers
# violations in the AVX2 sieve buffer casting code. 
# Todo: audit __m256i casts throughout siqs when using avx2 only.
ifeq ($(OS),Windows_NT)
    CFLAGS += -fno-strict-aliasing -DULL_NO_UL -DBITS_PER_GMP_ULONG=32
endif

VBITS ?= 64
CFLAGS += -DVBITS=$(VBITS)

# Include paths — project-internal
CFLAGS += \
    -I. \
    -Ifactor/shared/include \
    -Itop/cmdParser \
    -Itop \
    -Ifactor/siqs/include \
    -Ifactor/ecm/include \
    -Ifactor/mpqs/include \
    -Ifactor/nfs/gnfs/include \
    -Ifactor/core/include \
    -Ifactor/ecm \
    -Ifactor/shared/ytools/include \
    -Ifactor/shared/ysieve/include \
    -Ifactor/shared/common/include \
    -Ifactor/shared/aprcl/include \
    -Ifactor/shared/arith/include \
    -Ifactor/shared/cub/include \
    -Ifactor/shared/arith/tfm/include \
    -Ifactor/shared/common/filter/include \
    -Ifactor/shared/common/lanczos/include \
    -Ifactor/shared/common/lanczos/cpu/include \
    -Ifactor/shared/common/lanczos/gpu/include \
    -Ifactor/nfs/gnfs \
    -Ifactor/nfs/gnfs/poly \
    -Ifactor/nfs/gnfs/poly/stage1 \
    -Ifactor/nfs/gnfs/filter/include \
    -Ifactor/nfs/gnfs/poly/include \
    -Ifactor/nfs/gnfs/poly/stage1/include \
    -Ifactor/nfs/gnfs/poly/stage1/stage1_core_gpu/include \
    -Ifactor/nfs/gnfs/poly/stage2/include \
    -Ifactor/nfs/gnfs/sieve/include \
    -Ifactor/nfs/gnfs/sqrt/include \
    -Ifactor

# External dependency includes
CFLAGS += $(GMP_INC) $(ECM_INC) $(CUDA_INC) $(OCL_INC)


# -----------------------------------------------------------------------------
# 7. DEBUG / RELEASE FLAGS
# -----------------------------------------------------------------------------
ifeq ($(DEBUG),1)
    CFLAGS += -O0 -g -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG -D_FILE_OFFSET_BITS=64 -fomit-frame-pointer
endif


# -----------------------------------------------------------------------------
# 8. PER-COMPILER FLAGS
# -----------------------------------------------------------------------------
ifeq ($(COMPILER_FAMILY),icc)
    CFLAGS += -qopt-report=5
    # ICC uses -L path for its own runtime headers (legacy icc on RHEL)
    CFLAGS += -L/usr/lib/gcc/x86_64-redhat-linux/4.4.4

else ifeq ($(COMPILER_FAMILY),clang)
    CFLAGS += \
        -Wno-unused-parameter \
        -Wno-padded \
        -Wno-declaration-after-statement
    ifeq ($(DEBUG),1)
        CFLAGS        += -fsanitize=address,undefined -fno-omit-frame-pointer
        LDFLAGS_EXTRA += -fsanitize=address,undefined
# don't use link time optimizations
#    else
#        CFLAGS        += -flto
#        LDFLAGS_EXTRA += -flto
    endif

else
    # GCC
	# newer gcc versions with -O2 turn on auto-vectorization features that 
	# interfere with and greatly slow down the hand-optimized vectorization
	# in yafu's SIQS.  Turn them off here.
    CFLAGS += -Wshadow -Wstrict-prototypes -fno-tree-loop-vectorize -fno-tree-slp-vectorize
    ifeq ($(DEBUG),1)
        # nothing extra
# don't use link time optimizations
#    else
#        CFLAGS        += -flto
#        LDFLAGS_EXTRA += -flto
    endif
    ifeq ($(DETECTED_OS),Windows)
        ifdef STATIC_WIN
            # Fully static binary — runs in PowerShell/cmd.exe with no DLLs needed.
            # Requires that GMP, ECM, zlib etc. were installed as static (.a) libs
            # (pacman -S mingw-w64-x86_64-gmp etc. includes both static and shared).
            LDFLAGS_EXTRA += -static -static-libgcc
        else
            LDFLAGS_EXTRA += -static-libgcc
        endif
    endif
endif


# -----------------------------------------------------------------------------
# 9. ISA / MICRO-ARCHITECTURE FLAGS
#    Enabling a higher level automatically enables the levels below it.
# -----------------------------------------------------------------------------

# --- USE_NATIVE: auto-detect ISA from the build CPU --------------------------
# 通用构建必须在探测和级联启用指令集之前清除配置中的特定指令集选项。
ifeq ($(FORCE_GENERIC),1)
    override USE_NATIVE :=
    override USE_SSE41 :=
    override USE_AVX2 :=
    override USE_BMI2 :=
    override USE_AVX512 :=
    override USE_AVX512IFMA :=
    override USE_AVX512PF :=
    override ICELAKE :=
    override SKYLAKEX :=
    override KNL :=
endif
#
# Queries the compiler for the preprocessor macros it defines when targeting
# the native CPU (-march=native -dM -E).  This approach works identically
# with gcc, clang, and icc — all three support -dM -E and emit the same
# __SSE4_1__, __AVX2__, __AVX512F__ etc. macro names.
#
# Individual USE_* flags already set by the user are NOT overridden, so
# explicit overrides like USE_AVX512=0 are still respected even with
# USE_NATIVE=1.
#
# -march=native is added to CFLAGS here.  The specific -march=<arch> flags
# in the individual ISA blocks below (e.g. -march=skylake-avx512) are skipped
# when USE_NATIVE is set to avoid overriding or conflicting with the native
# target — -march=native already covers everything they would enable.
# The feature-enablement flags (-mavx2, -msse4.1 etc.) are safe alongside
# -march=native and are kept.
# -----------------------------------------------------------------------------
ifeq ($(USE_NATIVE),1)
    ifeq ($(DETECTED_OS),Windows)
        $(error USE_NATIVE is not supported on Windows/MinGW builds. \
            -march=native does not reliably enable the expected ISA instructions \
            under MinGW, resulting in a binary that compiles but runs slowly or \
            incorrectly. Use explicit ISA flags instead — for example: \
            USE_AVX2=1 USE_BMI2=1, or USE_AVX512=1. \
            See 'make help' for the full list.)
    endif
    # Dump all preprocessor macros the compiler defines for the native CPU.
    # The 'echo |' idiom feeds an empty C source — portable across platforms.
    _NATIVE_DEFS := $(shell echo | $(CC) -march=native -dM -E -x c - 2>/dev/null)

    ifndef USE_SSE41
        ifneq (,$(findstring __SSE4_1__,$(_NATIVE_DEFS)))
            USE_SSE41 := 1
        endif
    endif
    ifndef USE_AVX2
        ifneq (,$(findstring __AVX2__,$(_NATIVE_DEFS)))
            USE_AVX2 := 1
        endif
    endif
    ifndef USE_BMI2
        ifneq (,$(findstring __BMI2__,$(_NATIVE_DEFS)))
            USE_BMI2 := 1
        endif
    endif
    ifndef USE_AVX512
        ifneq (,$(findstring __AVX512F__,$(_NATIVE_DEFS)))
            USE_AVX512 := 1
        endif
    endif
    ifndef USE_AVX512IFMA
        ifneq (,$(findstring __AVX512IFMA__,$(_NATIVE_DEFS)))
            USE_AVX512IFMA := 1
        endif
    endif
    ifndef USE_AVX512PF
        ifneq (,$(findstring __AVX512PF__,$(_NATIVE_DEFS)))
            USE_AVX512PF := 1
        endif
    endif

    $(info USE_NATIVE: SSE41=$(USE_SSE41) AVX2=$(USE_AVX2) BMI2=$(USE_BMI2) AVX512=$(USE_AVX512) AVX512IFMA=$(USE_AVX512IFMA) AVX512PF=$(USE_AVX512PF))

    CFLAGS += -march=native
endif


# --- Implication chain -------------------------------------------------------
#
#   USE_AVX512IFMA  →  USE_AVX512
#   USE_AVX512PF    →  USE_AVX512
#   USE_AVX512      →  USE_AVX2  →  USE_SSE41
#   USE_AVX512      →  USE_BMI2
#
# Backward-compatibility aliases: the old microarchitecture names still work
# but map to the new feature flags and print a deprecation notice.
# -----------------------------------------------------------------------------
ifdef ICELAKE
    $(info NOTE: ICELAKE is deprecated — use USE_AVX512IFMA=1 instead)
    USE_AVX512IFMA := 1
endif
ifdef SKYLAKEX
    $(info NOTE: SKYLAKEX is deprecated — use USE_AVX512=1 instead)
    USE_AVX512 := 1
endif
ifdef KNL
    $(info NOTE: KNL is deprecated — use USE_AVX512PF=1 (and SMALLINT=1 if needed) instead)
    USE_AVX512PF := 1
endif

# Propagate implications top-down
ifeq ($(USE_AVX512IFMA),1)
    USE_AVX512 := 1
endif
ifeq ($(USE_AVX512PF),1)
    USE_AVX512 := 1
endif
ifeq ($(USE_AVX512),1)
    USE_AVX2   := 1
    USE_BMI2   := 1
endif
ifeq ($(USE_BMI2),1)
    USE_AVX2   := 1
endif
ifeq ($(USE_AVX2),1)
    USE_SSE41  := 1
endif

# --- SSE 4.1 -----------------------------------------------------------------
ifeq ($(USE_SSE41),1)
    CFLAGS += -msse4.1 -DUSE_SSE41
endif

# --- AVX2 --------------------------------------------------------------------
ifeq ($(USE_AVX2),1)
    CFLAGS += -DUSE_AVX2
    ifeq ($(COMPILER_FAMILY),icc)
        ifneq ($(USE_NATIVE),1)
            CFLAGS += -march=core-avx2
        endif
    else
        CFLAGS += -mavx2
    endif
endif

# --- BMI / BMI2 --------------------------------------------------------------
ifeq ($(USE_BMI2),1)
    CFLAGS += -mbmi -mbmi2 -DUSE_BMI2
endif

# --- AVX-512 base (F + BW) ---------------------------------------------------
# Works on: Skylake-X, Cascade Lake, Ice Lake, Tiger Lake, Zen 4, and others.
# Use this flag for any CPU with AVX-512F/BW — not just Intel server parts.
ifeq ($(USE_AVX512),1)
    # New-style defines
    CFLAGS += -DUSE_AVX512F -DUSE_AVX512BW
    # Legacy defines retained for source compatibility during transition
    CFLAGS += -DSKYLAKEX
    ifneq ($(USE_NATIVE),1)
        # Skip -march=<arch> when USE_NATIVE is set: -march=native already
        # covers this and the specific arch could conflict with native target.
        ifeq ($(COMPILER_FAMILY),icc)
            CFLAGS += -march=core-avx512
        else
            CFLAGS += -march=skylake-avx512
        endif
    endif
endif

# --- AVX-512 IFMA (Ice Lake and later) ---------------------------------------
ifeq ($(USE_AVX512IFMA),1)
    # New-style define
    CFLAGS += -DUSE_AVX512IFMA
    # Legacy define retained for source compatibility during transition
    CFLAGS += -DIFMA
    ifneq ($(USE_NATIVE),1)
        CFLAGS += -march=icelake-client
    endif
endif

# --- AVX-512 PF (Xeon Phi / Knights Landing) ---------------------------------
ifeq ($(USE_AVX512PF),1)
    # New-style define
    CFLAGS += -DUSE_AVX512PF -DTARGET_KNL
    ifneq ($(USE_NATIVE),1)
        ifeq ($(COMPILER_FAMILY),icc)
            CFLAGS += -xMIC-AVX512
        else
            CFLAGS += -march=knl
        endif
    endif
endif


# -----------------------------------------------------------------------------
# 10. OPTIONAL FEATURE FLAGS
# -----------------------------------------------------------------------------
# OpenMP drives the parallel ECM and sieve stages, and HAVE_GMP_ECM selects
# the gmp-ecm implementation; neither is optional here.
CFLAGS += -fopenmp -DHAVE_OMP
CFLAGS += -DHAVE_GMP_ECM

ifdef BATCH_CUDA
    CFLAGS += -DHAVE_CUDA_BATCH_FACTOR -DTOOLKIT_VERSION=$(TOOLKIT_VERSION)
    TOOLKIT_VERSION ?= 12
    CFLAGS += -DTOOLKIT_VERSION=$(TOOLKIT_VERSION)
endif

ifdef CUDA_POLY
    CFLAGS += -DHAVE_CUDA_POLY -DTOOLKIT_VERSION=$(TOOLKIT_VERSION) -Ifactor/shared/cub/include
	CUDA_PTX_ARCH ?= compute_$(SM)
	CUB_ENGINE_ARCH ?= -gencode arch=compute_$(SM),code=sm_$(SM)
    ifeq ($(DETECTED_OS),Windows)
        CUDA_POLY_LIBS := "$(CUDA_LIBDIR)/cuda.lib"
    else
        CUDA_POLY_LIBS := -lcuda 
#-lcudart
# -L/usr/local/cuda-12.8/targets/x86_64-linux/lib/ -lcuda -lcudart_static
    endif
endif

# ifdef CUDA_LA
#     CFLAGS += -DHAVE_CUDA_LA
# endif

ifeq ($(MPI),1)
    CFLAGS += -DHAVE_MPI
endif

ifeq ($(BOINC),1)
    BOINC_INC_DIR ?= .
    BOINC_LIB_DIR ?= .
    CFLAGS += -I$(BOINC_INC_DIR) -DHAVE_BOINC
endif

ifeq ($(CUDAAWARE),1)
    CFLAGS += -DHAVE_CUDAAWARE_MPI
endif

ifeq ($(SMALLINT),1)
    CFLAGS += -DSMALL_SIQS_INTERVALS
endif

ifeq ($(PROFILE),1)
    CFLAGS  += -pg -DPROFILING
    BINNAME := ${BINNAME:%=%_prof}
endif

ifeq ($(OPT_DEBUG),1)
    CFLAGS += -DOPT_DEBUG
endif

ifeq ($(TIMING),1)
    CFLAGS += -DQS_TIMING
endif

ifeq ($(FORCE_GENERIC),1)
    CFLAGS += -DFORCE_GENERIC
endif

ifeq ($(STATIC),1)
    LDFLAGS_EXTRA += -static
    ifeq ($(COMPILER_FAMILY),icc)
        LDFLAGS_EXTRA += -static-intel
    endif
endif

# Append any user overrides from config.mk
CFLAGS += $(USER_CFLAGS)


# -----------------------------------------------------------------------------
# 11. LINKER FLAG ASSEMBLY
#
#    LIBS       — libraries linked into yafu and the demo binaries
# -----------------------------------------------------------------------------

# Base library search paths
LIBS        := -L. $(GMP_LPATH) $(ECM_LPATH) $(CUDA_LPATH) $(OCL_LPATH)

# ECM.  The elliptic-curve method is one of the two ways this program finds
# medium-sized factors, so gmp-ecm is required rather than a build option.
ifneq (,$(HAVE_ECM_LIB))
    LIBS        += -lecm
else
    $(error ecm.h not found: the ECM factoring method needs gmp-ecm.             Install libecm-dev, or point ECM_PREFIX / ECM_INCDIR + ECM_LIBDIR at it)
endif

# CUDA (batch cofactorisation)
ifdef BATCH_CUDA
    LIBS        += $(CUDA_LPATH) -lcuda -lcudart
endif

# CUDA (polynomial selection)
ifdef CUDA_POLY
	LIBS        += $(CUDA_LPATH) $(CUDA_POLY_LIBS)
endif

# OpenCL
ifneq (,$(HAVE_OPENCL))
    LIBS        += $(OCL_LPATH) $(OCL_LIBS)
endif

# MPI / BOINC
# GMP, math, threading
LIBS        += -lgmp -lpthread -lm

# Static Windows build needs extra libs that are normally pulled in implicitly
# by the shared runtime, plus winpthread for the static pthreads implementation.
ifeq ($(DETECTED_OS),Windows)
    ifdef STATIC_WIN
        LIBS        += -lwinpthread -lws2_32 -lssp
    endif
endif

# dl (not needed on MinGW or Windows)
ifneq ($(MINGW),1)
    ifneq ($(DETECTED_OS),Windows)
        LIBS        += -ldl
    endif
endif

# ICC: SVML
ifeq ($(COMPILER_FAMILY),icc)
    LIBS        += -lsvml
endif

LIBS        += $(USER_LDFLAGS) $(LDFLAGS_EXTRA)


# =============================================================================
# SOURCE FILE LISTS
# =============================================================================

OBJ_EXT := .o

# -----------------------------------------------------------------------------
# 12. MSIEVE / YAFU shared sources
# -----------------------------------------------------------------------------
MSIEVE_YAFU_SRCS = \
    factor/siqs/msieve/lanczos.c \
    factor/siqs/msieve/lanczos_matmul0.c \
    factor/siqs/msieve/lanczos_matmul1.c \
    factor/siqs/msieve/lanczos_matmul2.c \
    factor/siqs/msieve/lanczos_pre.c \
    factor/siqs/msieve/sqrt.c \
    factor/siqs/msieve/gf2.c

MSIEVE_YAFU_OBJS = $(MSIEVE_YAFU_SRCS:.c=$(OBJ_EXT))


# -----------------------------------------------------------------------------
# 13. YAFU top-level sources
# -----------------------------------------------------------------------------
YAFU_SRCS = \
    top/driver.c \
    top/test.c \
    factor/core/tune.c \
    factor/core/autofactor.c \
    top/cmdParser/cmdOptions.c \
    top/cmdParser/calc.c


# -----------------------------------------------------------------------------
# 14. COMMON (shared across yafu targets)
# -----------------------------------------------------------------------------
COMMON_SRCS = \
    factor/core/batch_factor.c \
    factor/core/factor_common.c \
    factor/trialdiv/rho.c \
    factor/trialdiv/squfof.c \
    factor/trialdiv/trialdiv.c \
    factor/shared/arith/arith.c \
    factor/shared/arith/monty.c \
    factor/shared/arith/fftmul.c \
    factor/shared/aprcl/tinyprp.c \
    factor/ecm/tinyecm.c \
    factor/ecm/micropm1.c \
    factor/ecm/microecm.c \
    factor/shared/ytools/threadpool.c \
    factor/shared/ytools/ytools.c \
    factor/shared/ysieve/presieve.c \
    factor/shared/ysieve/count.c \
    factor/shared/ysieve/offsets.c \
    factor/shared/ysieve/primes.c \
    factor/shared/ysieve/roots.c \
    factor/shared/ysieve/linesieve.c \
    factor/shared/ysieve/soe.c \
    factor/shared/ysieve/tiny.c \
    factor/shared/ysieve/worker.c \
    factor/shared/ysieve/soe_util.c \
    factor/shared/ysieve/wrapper.c \
    factor/shared/aprcl/mpz_aprcl.c \
    factor/core/gpu_cofactorization.c \
	factor/shared/common/vec_bitonic_sort.c

COMMON_BATCH_GPU_SRCS = \
    factor/core/cuda_tinyecm.cu \
    factor/core/cuda_intrinsics.h


# -----------------------------------------------------------------------------
# 15. ECM sources
# -----------------------------------------------------------------------------
ECM_SRCS = \
    factor/ecm/ecm.c \
    factor/ecm/pp1.c \
    factor/ecm/pm1.c \
    factor/ecm/avxecm.c \
    factor/ecm/avx_ecm_main.c \
    factor/ecm/vec_common.c \
    factor/ecm/vecarith.c \
    factor/ecm/vecarith52.c \
    factor/ecm/vecarith52_special.c \
    factor/ecm/vecarith52_common.c


# -----------------------------------------------------------------------------
# 16. SIQS sources  (base + ISA-specific additions)
# -----------------------------------------------------------------------------
YAFU_SIQS_SRCS = \
    factor/siqs/filter.c \
    factor/siqs/tdiv.c \
    factor/siqs/tdiv_small.c \
    factor/siqs/tdiv_large.c \
    factor/siqs/tdiv_scan.c \
    factor/siqs/large_sieve.c \
    factor/siqs/new_poly.c \
    factor/siqs/siqs_test.c \
    factor/siqs/siqs_aux.c \
    factor/pmpqs/pmpqs.c \
    factor/siqs/SIQS.c \
    factor/siqs/med_sieve_32k.c \
    factor/siqs/poly_roots_32k.c \
    factor/siqs/cofactorize_siqs.c

ifeq ($(USE_SSE41),1)
    YAFU_SIQS_SRCS += \
        factor/siqs/update_poly_roots_32k_sse4.1.c \
        factor/siqs/med_sieve_32k_sse4.1.c
endif

ifeq ($(USE_AVX2),1)
    YAFU_SIQS_SRCS += \
        factor/siqs/tdiv_med_32k_avx2.c \
        factor/siqs/update_poly_roots_32k_avx2.c \
        factor/siqs/med_sieve_32k_avx2.c \
        factor/siqs/tdiv_resieve_32k_avx2.c
endif

ifeq ($(USE_AVX512),1)
    YAFU_SIQS_SRCS += factor/siqs/update_poly_roots_32k_knl.c
endif

# Always-included generic SIQS files (appended after any ISA variants)
YAFU_SIQS_SRCS += \
    factor/siqs/update_poly_roots_32k.c \
    factor/siqs/tdiv_med_32k.c \
    factor/siqs/tdiv_resieve_32k.c


# -----------------------------------------------------------------------------
# 18. NFS sources
# -----------------------------------------------------------------------------
YAFU_NFS_SRCS = \
    factor/nfs/nfs_sieving.c \
    factor/nfs/nfs_poly.c \
    factor/nfs/nfs_postproc.c \
    factor/nfs/nfs_filemanip.c \
    factor/nfs/nfs_threading.c \
    factor/nfs/snfs.c \
    factor/nfs/nfs.c

ifdef BATCH_CUDA
    YAFU_NFS_SRCS += factor/shared/common/cuda_xface.c
endif

NFS_SRCS = \
    factor/nfs/gnfs/poly/poly.c \
    factor/nfs/gnfs/poly/poly_param.c \
    factor/nfs/gnfs/poly/poly_skew.c \
	factor/nfs/gnfs/poly/poly_stats.c \
    factor/nfs/gnfs/poly/polyutil.c \
    factor/nfs/gnfs/poly/root_score.c \
    factor/nfs/gnfs/poly/size_score.c \
	factor/nfs/gnfs/poly/stage1/stage1_sieve_cpu_hashtable.c \
	factor/nfs/gnfs/poly/stage1/stage1_sieve_cpu.c \
	factor/nfs/gnfs/poly/stage1/stage1_engine.c \
    factor/nfs/gnfs/poly/stage1/stage1.c \
    factor/nfs/gnfs/poly/stage1/stage1_roots.c \
    factor/nfs/gnfs/poly/stage2/optimize.c \
    factor/nfs/gnfs/poly/stage2/optimize_deg6.c \
    factor/nfs/gnfs/poly/stage2/root_sieve.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_deg45_x.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_deg5_xy.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_deg6_x.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_deg6_xy.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_deg6_xyz.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_line.c \
    factor/nfs/gnfs/poly/stage2/root_sieve_util.c \
    factor/nfs/gnfs/poly/stage2/stage2.c \
    factor/nfs/gnfs/filter/duplicate.c \
    factor/nfs/gnfs/filter/filter.c \
    factor/nfs/gnfs/filter/singleton.c \
    factor/nfs/gnfs/sieve/sieve_line.c \
    factor/nfs/gnfs/sieve/sieve_util.c \
    factor/nfs/gnfs/sqrt/sqrt.c \
    factor/nfs/gnfs/sqrt/sqrt_a.c \
    factor/nfs/gnfs/fb.c \
    factor/nfs/gnfs/ffpoly.c \
    factor/nfs/gnfs/gf2.c \
    factor/nfs/gnfs/gnfs.c \
    factor/nfs/gnfs/relation.c

NFS_GPU_SRCS  = factor/nfs/gnfs/poly/stage1/stage1_sieve_gpu.c
NFS_NOGPU_SRCS = 
#factor/nfs/gnfs/poly/stage1/stage1_sieve_cpu.c

ifeq ($(CUDA_POLY),1)
    NFS_SRCS += $(NFS_GPU_SRCS)
else
    NFS_SRCS += $(NFS_NOGPU_SRCS)
endif


# -----------------------------------------------------------------------------
# 19. Msieve common sources
# -----------------------------------------------------------------------------
MSIEVE_COMMON_SRCS = \
    factor/shared/common/filter/clique.c \
    factor/shared/common/filter/filter.c \
    factor/shared/common/filter/merge.c \
    factor/shared/common/filter/merge_post.c \
    factor/shared/common/filter/merge_pre.c \
    factor/shared/common/filter/merge_util.c \
    factor/shared/common/filter/singleton.c \
    factor/shared/common/lanczos/lanczos.c \
    factor/shared/common/lanczos/lanczos_io.c \
    factor/shared/common/lanczos/lanczos_matmul.c \
    factor/shared/common/lanczos/lanczos_pre.c \
    factor/shared/common/lanczos/matmul_util.c \
    factor/shared/common/smallfact/gmp_ecm.c \
    factor/shared/common/smallfact/smallfact.c \
    factor/shared/common/smallfact/squfof.c \
    factor/shared/common/smallfact/tinyqs.c \
    factor/shared/common/cuda_xface.c \
    factor/shared/common/cuda_xface_la.c \
    factor/shared/common/dickman.c \
    factor/shared/common/driver.c \
    factor/shared/common/expr_eval.c \
    factor/shared/common/hashtable.c \
    factor/shared/common/integrate.c \
    factor/shared/common/minimize.c \
    factor/shared/common/minimize_global.c \
    factor/shared/common/mp.c \
    factor/shared/common/ms_batch_factor.c \
    factor/shared/common/polyroot.c \
    factor/shared/common/prime_delta.c \
    factor/shared/common/prime_sieve.c \
    factor/shared/common/savefile.c \
    factor/shared/common/strtoll.c \
    factor/shared/common/thread.c \
    factor/shared/common/util.c \
    factor/shared/aprcl/mpz_aprcl32.c
	
ifeq ($(OS),Windows_NT)
	MSIEVE_COMMON_SRCS += factor/shared/common/mpz-ull.c
endif

COMMON_GPU_SRCS = \
    factor/shared/common/lanczos/gpu/lanczos_matmul_gpu.c \
    factor/shared/common/lanczos/gpu/lanczos_vv.c

COMMON_NOGPU_SRCS = \
    factor/shared/common/lanczos/cpu/lanczos_matmul0.c \
    factor/shared/common/lanczos/cpu/lanczos_matmul1.c \
    factor/shared/common/lanczos/cpu/lanczos_matmul2.c \
    factor/shared/common/lanczos/cpu/lanczos_vv.c

ifdef CUDA_LA
    MSIEVE_COMMON_SRCS += $(COMMON_GPU_SRCS)
else
    MSIEVE_COMMON_SRCS += $(COMMON_NOGPU_SRCS)
endif


# -----------------------------------------------------------------------------
# 20. QS sources  (the multiple polynomial quadratic sieve.
#     yafu code reaches it only through mpqs_xface.h, because
#     mpqs.h and yafu disagree on the size of mp_t)
# -----------------------------------------------------------------------------
QS_SRCS = \
    factor/mpqs/gf2.c \
    factor/mpqs/mpqs.c \
    factor/mpqs/mpqs_xface.c \
    factor/mpqs/poly.c \
    factor/mpqs/relation.c \
    factor/mpqs/sieve.c \
    factor/mpqs/sieve_core.c \
    factor/mpqs/sqrt.c


# -----------------------------------------------------------------------------
# 21. OBJECT LISTS
# -----------------------------------------------------------------------------
YAFU_OBJS         = $(YAFU_SRCS:.c=$(OBJ_EXT))
YAFU_SIQS_OBJS    = $(YAFU_SIQS_SRCS:.c=$(OBJ_EXT))
YAFU_ECM_OBJS     = $(ECM_SRCS:.c=$(OBJ_EXT))
YAFU_COMMON_OBJS  = $(COMMON_SRCS:.c=$(OBJ_EXT))
YAFU_NFS_OBJS     = $(YAFU_NFS_SRCS:.c=$(OBJ_EXT))
MSIEVE_COMMON_OBJS = $(MSIEVE_COMMON_SRCS:.c=$(OBJ_EXT))
NFS_OBJS          = $(NFS_SRCS:.c=.no)
NFS_GPU_OBJS      = $(NFS_GPU_SRCS:.c=.no)
NFS_NOGPU_OBJS    = $(NFS_NOGPU_SRCS:.c=.no)
QS_OBJS = \
    factor/mpqs/gf2.qo \
    factor/mpqs/mpqs.qo \
    factor/mpqs/mpqs_xface.qo \
    factor/mpqs/poly.qo \
    factor/mpqs/relation.qo \
    factor/mpqs/sieve.qo \
    factor/mpqs/sqrt.qo \
    factor/mpqs/sieve_core_generic_32k.qo \
    factor/mpqs/sieve_core_generic_64k.qo

DEPS_DIR      := .deps
ALL_OBJS      := $(YAFU_OBJS) $(YAFU_SIQS_OBJS) $(YAFU_ECM_OBJS) \
                 $(YAFU_COMMON_OBJS) $(YAFU_NFS_OBJS) \
                 $(MSIEVE_YAFU_OBJS) $(MSIEVE_COMMON_OBJS)

# All compiled objects across all extensions — used to derive the full set
# of .deps subdirectories that need to exist before compilation starts.
ALL_COMPILED  := $(ALL_OBJS) $(QS_OBJS) $(NFS_OBJS)

# Mirror the source tree under .deps/ for every compiled object.
# .o → .deps/path/to/file.d
# .qo → .deps/path/to/file.d  (strip the q, keep the path)
# .no → .deps/path/to/file.d  (strip the n, keep the path)
ALL_DEPS      := $(patsubst %.o,$(DEPS_DIR)/%.d,\
                 $(patsubst %.qo,$(DEPS_DIR)/%.d,\
                 $(patsubst %.no,$(DEPS_DIR)/%.d,$(ALL_COMPILED))))
DEPS_SUBDIRS  := $(sort $(DEPS_DIR)/ $(dir $(ALL_DEPS)))

# GPU / PTX objects
GPU_OBJS :=
ifdef CUDA_LA
    GPU_OBJS += lanczos_kernel.ptx
endif
ifeq ($(CUDA_POLY),1)
    GPU_OBJS += stage1_core.ptx factor/shared/cub/built
endif
ifdef BATCH_CUDA
    BATCH_GPU_OBJS := cuda_ecm$(SM).ptx
else
    BATCH_GPU_OBJS :=
endif


# =============================================================================
# BUILD RULES
# =============================================================================

# -----------------------------------------------------------------------------
# 22. PHONY TARGETS
# -----------------------------------------------------------------------------
.PHONY: all yafu lasieve lasieve-clean clean info help _dep_status

# -----------------------------------------------------------------------------
# 23. DEFAULT GOAL
# -----------------------------------------------------------------------------
all: yafu lasieve

# -----------------------------------------------------------------------------
# 24. DEPENDENCY STATUS  (printed at the start of every real build)
# -----------------------------------------------------------------------------
_dep_status:
	@echo "--- Dependency status -----------------------------------------------"
	@echo "  GMP     : found ($(GMP_INCDIR))"
	@echo "  GMP-ECM : found ($(ECM_INCDIR)) — required for the ECM method"
	@echo "  CUDA    : $(if $(HAVE_CUDA_TOOLKIT),found ($(CUDA_INCDIR)),DISABLED — cuda.h not found; set CUDA_PREFIX in config.mk)"
	@echo "  OpenCL  : $(if $(HAVE_OPENCL),found ($(OCL_INCDIR)),DISABLED — CL/cl.h not found; set OCL_PREFIX in config.mk)"
	@echo "---------------------------------------------------------------------"

# Create .deps subdirectory tree (order-only — never triggers a rebuild)
$(DEPS_SUBDIRS):
	$(MKDIR) $@


# 静态库是链接中间产物：放在 build/ 下，根目录不留生成文件
BUILD_DIR   := build
ARCHIVES    := $(BUILD_DIR)/libysiqs.a $(BUILD_DIR)/libyecm.a \
               $(BUILD_DIR)/libynfs.a $(BUILD_DIR)/libmsieve.a

# -----------------------------------------------------------------------------
# 25. LINK TARGETS
# -----------------------------------------------------------------------------

yafu: _dep_status $(YAFU_OBJS) $(ARCHIVES) $(GPU_OBJS)
	$(CC) $(CFLAGS) $(YAFU_OBJS) -o yafu$(EXE_EXT) \
	    $(ARCHIVES) $(LIBS)


# -----------------------------------------------------------------------------
# 25a. TEST SUITE  (testkit + layered module tests)
#
#   make test        build and link the test binary (yafu_test)
#   make test-run    build it, then run the full suite
#   make test-clean  remove test artefacts only
#
# The test objects compile with the same $(CFLAGS) as the library, so they
# resolve the same project headers (mp_platform.h, arith.h, monty.h, the aprcl
# headers, gmp.h); the extra -I$(TEST_DIR) lets the layer files find the
# framework headers testkit.h / test_data.h.
#
# Layer 1 exercises the arithmetic / number-theory kernels, which live in
# COMMON_SRCS (arith.c, monty.c, factor/shared/aprcl/{mpz_aprcl,tinyprp}.c, factor/shared/ysieve/*).
# We bundle YAFU_COMMON_OBJS into a static archive and let the linker pull only
# the members the tests actually reference, so the other COMMON objects
# (factor_common, microecm, gpu_cofactorization, ...) and their SIQS/NFS/CUDA
# dependencies are never dragged into the test binary.
#
# NOTE: if your tree has split the 128-bit / single-limb code out of arith.c /
# monty.c into limb1.c / limb2.c, add those to COMMON_SRCS (the main build needs
# them too) or append their objects to TEST_KERNEL_OBJS, so symbols such as
# mulmod128 resolve at link time.
# -----------------------------------------------------------------------------
.PHONY: test test-run test-clean

TEST_DIR  := test
TEST_SRCS := \
    $(TEST_DIR)/testkit.c \
    $(TEST_DIR)/test_data.c \
    $(TEST_DIR)/test_main.c \
    $(TEST_DIR)/layer0/test_mp_arith.c \
    $(TEST_DIR)/layer0/test_mp_bitscan.c \
    $(TEST_DIR)/layer1/test_sp_arith.c \
    $(TEST_DIR)/layer1/test_modular.c \
    $(TEST_DIR)/layer1/test_monty_review.c \
    $(TEST_DIR)/layer1/test_primality.c \
    $(TEST_DIR)/layer1/test_aprcl_review.c \
    $(TEST_DIR)/layer1/test_tinyprp_review.c \
    $(TEST_DIR)/layer1/test_sieve.c \
    $(TEST_DIR)/layer2/test_ecm.c
TEST_OBJS := $(TEST_SRCS:.c=$(OBJ_EXT))
TEST_BIN  := yafu_test$(EXE_EXT)

# YAFU objects the layered tests link against (overridable).
TEST_KERNEL_OBJS ?= $(YAFU_COMMON_OBJS)

# 测试对象同样跟踪所包含的头文件，修改算术实现后自动重编译。
$(TEST_DIR)/%.o: $(TEST_DIR)/%.c $(TEST_DIR)/testkit.h $(TEST_DIR)/test_data.h
	$(MKDIR) $(DEPS_DIR)/$(dir $<)
	$(CC) $(CFLAGS) -I$(TEST_DIR) -MMD -MP -MF $(DEPS_DIR)/$(patsubst %.c,%.d,$<) -c -o $@ $<

-include $(patsubst %.c,$(DEPS_DIR)/%.d,$(TEST_SRCS))

$(BUILD_DIR)/libyafu_common.a: $(TEST_KERNEL_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar r  $@ $(TEST_KERNEL_OBJS)
	ranlib $@

test: _dep_status $(TEST_OBJS) $(BUILD_DIR)/libyafu_common.a
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $(TEST_BIN) $(BUILD_DIR)/libyafu_common.a $(LIBS)
	@echo "built $(TEST_BIN) — run it:  ./$(TEST_BIN)   (try --list, --bench, --help)"

test-run: test
	./$(TEST_BIN)

test-clean:
	$(RM_RF) $(TEST_OBJS) $(BUILD_DIR) $(TEST_BIN) $(TEST_FULL_BIN) $(TEST_SAN_BIN)


# -----------------------------------------------------------------------------
# 25b. TEST SUITE -- Layer 3 (individual big-int routines: SIQS, ...)
#
#   make test-full       build+link Layers 0-3 (adds the factoring archives)
#   make test-full-run   build and run it
#
# Unlike Layers 0-2, these call into the full factoring library (SIQS/ECM/NFS/
# msieve), so we link the same four archives yafu itself uses -- but with the
# 测试提供 main()；计算器测试需要除 driver.o 外的前端对象。
# 测试源码使用 -DTK_WITH_LAYER3 直接编译链接，避免覆盖 make test 的对象文件。
# -----------------------------------------------------------------------------
.PHONY: test-full test-full-run

TEST_L3_SRCS  := $(TEST_DIR)/layer3/test_siqs.c $(TEST_DIR)/layer3/test_calc.c \
    $(TEST_DIR)/layer3/test_options.c \
    $(TEST_DIR)/layer3/test_ecm_review.c $(TEST_DIR)/layer3/test_qs_review.c \
    factor/shared/common/vec_bitonic_sort.c
TEST_FRONTEND_OBJS := $(filter-out top/driver$(OBJ_EXT),$(YAFU_OBJS))
TEST_FULL_BIN := yafu_test_full$(EXE_EXT)
TEST_SAN_BIN := yafu_test_sanitize$(EXE_EXT)

# 每个静态库只由一条规则生成，允许主程序、演示程序和测试并行链接。
$(BUILD_DIR)/libysiqs.a: $(YAFU_SIQS_OBJS) $(YAFU_COMMON_OBJS) $(MSIEVE_YAFU_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar r  $@ $(YAFU_SIQS_OBJS) $(YAFU_COMMON_OBJS) $(MSIEVE_YAFU_OBJS)
	ranlib $@
$(BUILD_DIR)/libyecm.a: $(YAFU_ECM_OBJS) $(YAFU_COMMON_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar r  $@ $(YAFU_ECM_OBJS) $(YAFU_COMMON_OBJS)
	ranlib $@
$(BUILD_DIR)/libynfs.a: $(YAFU_NFS_OBJS) $(YAFU_COMMON_OBJS) $(BATCH_GPU_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar r  $@ $(YAFU_NFS_OBJS) $(YAFU_COMMON_OBJS) $(BATCH_GPU_OBJS)
	ranlib $@
$(BUILD_DIR)/libmsieve.a: $(MSIEVE_COMMON_OBJS) $(QS_OBJS) $(NFS_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	ar r  $@ $(MSIEVE_COMMON_OBJS) $(QS_OBJS) $(NFS_OBJS)
	ranlib $@

test-full: _dep_status $(ARCHIVES) $(TEST_FRONTEND_OBJS)
	$(CC) $(CFLAGS) -DTK_WITH_LAYER3 -I$(TEST_DIR) \
	    $(TEST_SRCS) $(TEST_L3_SRCS) $(TEST_FRONTEND_OBJS) -o $(TEST_FULL_BIN) \
	    $(ARCHIVES) $(LIBS)
	@echo "built $(TEST_FULL_BIN) -- Layers 0-3 (incl. SIQS integration)"

test-full-run: test-full
	./$(TEST_FULL_BIN)

.PHONY: test-cli
test-cli: yafu
	sh $(TEST_DIR)/test_cli.sh ./yafu$(EXE_EXT)

.PHONY: test-standalone
test-standalone:
	CC="$(CC)" CFLAGS="$(USER_CFLAGS)" sh $(TEST_DIR)/test_nfs.sh
	CC="$(CC)" CFLAGS="$(USER_CFLAGS)" sh $(TEST_DIR)/test_lasieve.sh

# 为所有参与链接的项目源码启用检查；下次常规 make 会按配置记录自动重建。
.PHONY: test-sanitize
test-sanitize:
	$(MAKE) test-full USER_CFLAGS="$(USER_CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./$(TEST_FULL_BIN) --tag fast

# 只为本次回归涉及的源码和测试启用运行时检查，不覆盖常规对象文件。
.PHONY: test-calc-sanitize
test-calc-sanitize: _dep_status $(ARCHIVES) $(TEST_FRONTEND_OBJS)
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -DTK_WITH_LAYER3 -I$(TEST_DIR) $(TEST_SRCS) $(TEST_L3_SRCS) \
	    top/cmdParser/calc.c factor/core/factor_common.c \
	    $(filter-out top/cmdParser/calc$(OBJ_EXT),$(TEST_FRONTEND_OBJS)) \
	    -o $(TEST_SAN_BIN) $(ARCHIVES) $(LIBS)
	./$(TEST_SAN_BIN) calc


# -----------------------------------------------------------------------------
# lasieve — NFS sieve binaries (factor/nfs/lasieve)
#
# `all` depends on this: yafu drives the sievers by name, so they have
# to exist for NFS to work.  They stay separate programs (see
# factor/nfs/README.md for why) but building them is no longer a
# second manual step.
#
# Built separately from the main yafu targets since the sieve programs are
# standalone binaries invoked at runtime, not linked into yafu.
#
# USE_AVX512=1 in config.mk or on the command line enables AVX-512 sieve
# paths automatically (maps to AVX512_ALL=1 in the sub-make).
#
# 可执行文件与 yafu 写在同一目录（仓库根），yafu 按自身位置找到它们。
# asm/ 中只生成供链接使用的库。
# -----------------------------------------------------------------------------
LASIEVE_DIR  := factor/nfs/lasieve
# 筛法器和 yafu 一起落在仓库根：yafu 按自身可执行文件所在目录找它们，
# 不需要用户再配一个路径指回源码树。子 make 在 factor/nfs/lasieve/ 里执行，
# 到仓库根要上三级。
LASIEVE_BINDIR ?= ../../..
LASIEVE_VARS := \
    CC=$(CC) \
    BINDIR=$(LASIEVE_BINDIR) \
    GMP_INCDIR=$(GMP_INCDIR) \
    GMP_LIBDIR=$(GMP_LIBDIR) \
    DETECTED_OS=$(DETECTED_OS) \
    $(if $(filter 1,$(DEBUG)),DEBUG=1) \
    $(if $(filter 1,$(USE_AVX512)),AVX512_ALL=1)

lasieve: _dep_status
	$(MAKE) -C $(LASIEVE_DIR) alle $(LASIEVE_VARS)

lasieve-clean:
	$(MAKE) -C $(LASIEVE_DIR) clean BINDIR=$(LASIEVE_BINDIR)


# -----------------------------------------------------------------------------
# 26. COMPILE RULES
# -----------------------------------------------------------------------------

# 编译器或选项变化时重建对象；内容不变则保留文件时间，避免无效重编译。
BUILD_CONFIG := $(DEPS_DIR)/build-config
shell_quote = '$(subst ','"'"',$(1))'
.PHONY: _force_build_config
_force_build_config:

$(BUILD_CONFIG): _force_build_config | $(DEPS_DIR)/
	@printf '%s\n' $(call shell_quote,$(CC)) $(call shell_quote,$(CFLAGS)) > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

$(sort $(ALL_COMPILED) $(TEST_OBJS)): $(BUILD_CONFIG)

# Standard .c → .o
# -MF redirects the dependency file into .deps/, mirroring the source tree.
%.o: %.c | $(DEPS_SUBDIRS)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPS_DIR)/$*.d -c -o $@ $<

# QS objects (.qo) — also get dependency files now
factor/mpqs/sieve_core_generic_32k.qo: factor/mpqs/sieve_core.c | $(DEPS_SUBDIRS)
	$(CC) $(CFLAGS) -DBLOCK_KB=32 -DHAS_SSE2 \
	    -DROUTINE_NAME=qs_core_sieve_generic_32k \
	    -MMD -MP -MF $(DEPS_DIR)/factor/mpqs/sieve_core_generic_32k.d \
	    -c -o $@ $<

factor/mpqs/sieve_core_generic_64k.qo: factor/mpqs/sieve_core.c | $(DEPS_SUBDIRS)
	$(CC) $(CFLAGS) -DBLOCK_KB=64 -DHAS_SSE2 \
	    -DROUTINE_NAME=qs_core_sieve_generic_64k \
	    -MMD -MP -MF $(DEPS_DIR)/factor/mpqs/sieve_core_generic_64k.d \
	    -c -o $@ $<

%.qo: %.c | $(DEPS_SUBDIRS)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPS_DIR)/$*.d -c -o $@ $<

# NFS objects (.no) — add -Ifactor/nfs/gnfs for NFS-internal includes
%.no: %.c | $(DEPS_SUBDIRS)
	$(CC) $(CFLAGS) -Ifactor/nfs/gnfs -MMD -MP -MF $(DEPS_DIR)/$*.d -c -o $@ $<

# GPU / PTX rules
stage1_core.ptx: factor/nfs/gnfs/poly/stage1/stage1_core_gpu/stage1_core.cu $(NFS_GPU_HDR)
	$(NVCC) -arch $(CUDA_PTX_ARCH) -ptx -I. -Ifactor/shared/cub/include -Ifactor/nfs/gnfs -Ifactor/nfs/gnfs/poly/stage1 -o $@ $<
	
#stage1_core.ptx: factor/nfs/gnfs/poly/stage1/stage1_core_gpu/stage1_core.cu
#	$(NVCC) -arch sm_$(SM) -ptx -o $@ $<

lanczos_kernel.ptx: factor/shared/common/lanczos/gpu/lanczos_kernel.cu
	$(NVCC) -arch sm_$(SM) -ptx -DVBITS=$(VBITS) -o $@ $<

cuda_ecm$(SM).ptx: $(COMMON_BATCH_GPU_SRCS)
	$(NVCC) -arch sm_$(SM) -ptx -o $@ $<

# factor/shared/cub/built:
# 	cd cub && $(MAKE) WIN=$(WIN) WIN64=$(WIN64) VBITS=$(VBITS) sm=$(SM)0 && cd ..

factor/shared/cub/built: factor/shared/cub/sort_engine.cu factor/shared/cub/collision_engine.cu factor/shared/cub/collision_engine.h factor/shared/cub/collision_bucket.h
	$(NVCC) $(CUB_ENGINE_ARCH) --shared -Xcompiler -fPIC -o factor/shared/cub/sort_engine.so factor/shared/cub/sort_engine.cu
# The Gerbicz collision engine uses __match_any_sync, which requires
# compute capability 7.0 (Volta) or newer. For older GPUs, skip building it;
# the sort engine is the default and works on sm_60. (Do not pass
# collengine=gerbicz on such a build - see load_collision_engine().)
ifeq ($(shell [ -n "$(SM)" ] && [ "$(SM)" -ge 70 ] && echo yes),yes)
	$(NVCC) $(CUB_ENGINE_ARCH) --shared -Xcompiler -fPIC -I. -Ifactor/shared/cub/include -Ifactor/nfs/gnfs -Ifactor/nfs/gnfs/poly/stage1 -o factor/shared/cub/collision_engine.so factor/shared/cub/collision_engine.cu
else
	@echo "NOTE: SM=$(SM) < 70 (pre-Volta); skipping the Gerbicz collision engine (requires sm_70+). Building the sort engine only - do not pass collengine=gerbicz."
	@rm -f factor/shared/cub/collision_engine.so
endif
	touch factor/shared/cub/built
	
# -----------------------------------------------------------------------------
# 27. AUTOMATIC DEPENDENCY INCLUSION  (.d files from .deps/)
# -----------------------------------------------------------------------------
ifeq (,$(filter %clean info help,$(MAKECMDGOALS)))
    -include $(ALL_DEPS)
endif
 

# -----------------------------------------------------------------------------
# 28. CLEAN
# -----------------------------------------------------------------------------
# NOTE on .ptx: the kernel for the target architecture is generated into the
# working directory at build time and is not tracked.
# PTX module so that GPU batch factorization works on machines without nvcc.
# The build also generates .ptx modules (cuda_ecm$(SM).ptx, lanczos_kernel.ptx,
# stage1_core.ptx), and those should be cleaned. A bare `*.ptx` glob therefore
# deletes a tracked file and leaves the working tree dirty after `make clean`.
# Only remove the generated ones.
GENERATED_PTX := $(filter-out cuda_ecm80.ptx,$(wildcard *.ptx))

clean:
	$(RM_RF) \
	    $(MSIEVE_YAFU_OBJS) \
	    $(YAFU_OBJS) $(YAFU_NFS_OBJS) $(YAFU_SIQS_OBJS) \
	    $(YAFU_ECM_OBJS) $(YAFU_COMMON_OBJS) \
	    $(MSIEVE_COMMON_OBJS) \
	    $(QS_OBJS) $(NFS_OBJS) $(NFS_GPU_OBJS) $(NFS_NOGPU_OBJS) \
	    $(DEPS_DIR) \
	    $(ARCHIVES) $(BUILD_DIR) \
	    yafu$(EXE_EXT) \
	    $(GENERATED_PTX)
	$(RM_RF) $(TEST_OBJS) $(BUILD_DIR) $(TEST_BIN) $(TEST_FULL_BIN) $(TEST_SAN_BIN)
	@echo "Note: use 'make lasieve-clean' to also clean factor/nfs/lasieve"


# -----------------------------------------------------------------------------
# 29. INFO
# -----------------------------------------------------------------------------
info:
	@echo "================================================================"
	@echo "  yafu build configuration"
	@echo "================================================================"
	@echo "  Detected OS      : $(DETECTED_OS)"
	@echo "  Compiler (CC)    : $(CC)"
	@echo "  Compiler family  : $(COMPILER_FAMILY)"
	@echo "  Debug build      : $(if $(filter 1,$(DEBUG)),yes,no)"
	@echo "  VBITS            : $(VBITS)"
	@echo "----------------------------------------------------------------"
	@echo "  ISA flags"
	@echo "    USE_NATIVE     : $(if $(filter 1,$(USE_NATIVE)),yes,no)"
	@echo "    USE_SSE41      : $(if $(filter 1,$(USE_SSE41)),yes,no)"
	@echo "    USE_AVX2       : $(if $(filter 1,$(USE_AVX2)),yes,no)"
	@echo "    USE_BMI2       : $(if $(filter 1,$(USE_BMI2)),yes,no)"
	@echo "    USE_AVX512     : $(if $(filter 1,$(USE_AVX512)),yes,no)"
	@echo "    USE_AVX512IFMA : $(if $(filter 1,$(USE_AVX512IFMA)),yes,no)"
	@echo "    USE_AVX512PF   : $(if $(filter 1,$(USE_AVX512PF)),yes,no)"
	@echo "----------------------------------------------------------------"
	@echo "  Dependencies"
	@echo "    GMP     inc    : $(GMP_INCDIR)"
	@echo "    GMP     lib    : $(GMP_LIBDIR)"
	@echo "    GMP-ECM        : required ($(ECM_INCDIR))"
	@echo "    CUDA           : $(if $(HAVE_CUDA_TOOLKIT),enabled ($(CUDA_INCDIR)),disabled)"
	@echo "    CUDA root src  : $(if $(CUDA_ROOT),CUDA_ROOT env,$(if $(CUDA_PATH),CUDA_PATH env,$(if $(_NVCC_ON_PATH),nvcc on PATH,$(if $(CUDA_PREFIX),config.mk/cmdline,not found))))"
	@echo "    OpenCL         : $(if $(HAVE_OPENCL),enabled ($(OCL_INCDIR)),disabled)"
	@echo "    NVCC           : $(if $(NVCC),$(NVCC),n/a)"
	@echo "    SM target      : $(SM)$(if $(_SMS_RAW), (all cards: $(_SMS_RAW)))"
	@echo "----------------------------------------------------------------"
	@echo "  Optional features"
	@echo "    OMP            : yes (required)"
	@echo "    BATCH_CUDA     : $(if $(BATCH_CUDA),yes,no)"
	@echo "    CUDA_POLY      : $(if $(CUDA_POLY),yes,no)"
	@echo "    MPI            : $(if $(filter 1,$(MPI)),yes,no)"
	@echo "    STATIC_WIN     : $(if $(STATIC_WIN),yes,no)"
	@echo "----------------------------------------------------------------"
	@echo "  CFLAGS           : $(CFLAGS)"
	@echo "  LIBS             : $(LIBS)"
	@echo "----------------------------------------------------------------"
	@echo "  lasieve (NFS sieve — separate target)"
	@echo "    Directory      : $(LASIEVE_DIR)"
	@echo "    AVX-512        : $(if $(filter 1,$(USE_AVX512)),enabled (AVX512_ALL=1),disabled)"
	@echo "================================================================"


# -----------------------------------------------------------------------------
# 30. HELP
# -----------------------------------------------------------------------------
help:
	@echo ""
	@echo "  Targets:"
	@echo "    make yafu            build the main yafu binary"
	@echo "    make all             build all four targets above"
	@echo "    make test            build the test suite (yafu_test)"
	@echo "    make test-run        build and run the test suite"
	@echo "    make test-clean      remove test build artefacts"
	@echo "    make test-full       build Layers 0-3 (SIQS and calculator integration)"
	@echo "    make test-cli        run command-line regressions"
	@echo "    make test-standalone run isolated NFS and lasieve regressions"
	@echo "    make test-sanitize   rebuild all test dependencies with ASan/UBSan and run fast tests"
	@echo "    make test-calc-sanitize  build+run calculator regressions with ASan/UBSan"
	@echo "    make test-full-run   build and run Layers 0-3"
	@echo "    make lasieve         build the NFS sievers next to the yafu executable"
	@echo "    make clean           remove yafu build artefacts (not lasieve)"
	@echo "    make lasieve-clean   remove lasieve build artefacts"
	@echo "    make info            show resolved flags and dependency paths"
	@echo ""
	@echo "  Compiler selection (default: gcc):"
	@echo "    make CC=gcc          GCC"
	@echo "    make CC=clang        Clang"
	@echo "    make CC=icc          Intel C Compiler"
	@echo ""
	@echo "  Build type:"
	@echo "    make DEBUG=1         debug build (-O0 -g)"
	@echo ""
	@echo "  ISA / micro-architecture (enabling a higher level implies lower ones):"
	@echo "    make USE_NATIVE=1       Auto-detect ISA from build CPU + -march=native"
	@echo "                            Works with gcc, clang, and icc on Linux."
	@echo "                            Not supported on Windows/MinGW."
	@echo "                            Not for cross-compilation or CI."
	@echo "    make USE_SSE41=1        SSE 4.1"
	@echo "    make USE_AVX2=1         AVX2  (implies SSE 4.1)"
	@echo "    make USE_BMI2=1         BMI / BMI2"
	@echo "    make USE_AVX512=1       AVX-512 F/BW  (implies AVX2, BMI2)"
	@echo "                            Also enables AVX512_ALL=1 for lasieve"
	@echo "                            Works on: Skylake-X, Ice Lake, Zen 4, etc."
	@echo "    make USE_AVX512IFMA=1   AVX-512 IFMA  (implies USE_AVX512)"
	@echo "                            Ice Lake and later"
	@echo "    make USE_AVX512PF=1     AVX-512 PF    (implies USE_AVX512)"
	@echo "                            Xeon Phi / Knights Landing only"
	@echo ""
	@echo "  Deprecated ISA aliases (still accepted, mapped to new flags):"
	@echo "    SKYLAKEX=1  →  USE_AVX512=1"
	@echo "    ICELAKE=1   →  USE_AVX512IFMA=1"
	@echo "    KNL=1       →  USE_AVX512PF=1"
	@echo ""
	@echo "  Optional features:"
	@echo "    make OMP=1           OpenMP threading"
	@echo "    make ECM=1           GMP-ECM support"
	@echo "    make BATCH_CUDA=1    GPU cofactorisation (NFS/SIQS)"
	@echo "    make CUDA_POLY=1     GPU NFS polynomial selection"
	@echo "    make MPI=1           MPI parallel processing"
	@echo "    make VBITS=128       linear algebra vector width (64/128/256)"
	@echo "    make SMALLINT=1      small SIQS intervals"
	@echo "    make PROFILE=1       gprof profiling"
	@echo "    make TIMING=1        QS timing instrumentation"
	@echo "    make FORCE_GENERIC=1 disable all SIMD paths"
	@echo "    make STATIC=1        static link (experimental, Linux)"
	@echo "    make STATIC_WIN=1    fully static Windows .exe (no DLLs needed)"
	@echo "    make MINGW=1         building under MinGW (skips -ldl)"
	@echo ""
	@echo "  Tip: set frequently-used flags in config.mk instead of typing"
	@echo "  them every time (copy config.mk.example to get started)."
	@echo ""
