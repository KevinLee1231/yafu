# Copyright (C) 2001,2002 Jens Franke
# This file is part of gnfs4linux, distributed under the terms of the
# GNU General Public Licence and WITHOUT ANY WARRANTY.
#
# You should have received a copy of the GNU General Public License along
# with this program; see the file COPYING.  If not, write to the Free
# Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
# 02111-1307, USA.
#
# -----------------------------------------------------------------------------
# factor/nfs/lasieve/Makefile
#
# Builds the NFS sieve executables:
#   gnfs-lasieve4I{11..16}e   (alle)
#   gnfs-lasieve4I{11..16}f   (allf)
#   gnfs-lasieve4I{11..16}g   (allg)
#
# Assembly libraries (liblasieve.a, liblasieveI%.a) are built in the asm/
# subdirectory and pulled in automatically via the asm/%: force delegation.
#
# Variables accepted from the top-level yafu Makefile (all have sensible
# defaults so this file can also be used standalone):
#
#   CC            Compiler: gcc, clang, or icc (default: gcc)
#   GMP_INCDIR    Path to gmp.h               (default: /usr/include)
#   GMP_LIBDIR    Path to libgmp.a            (default: /usr/lib)
#   DETECTED_OS   Windows or Linux            (auto-detected if absent)
#   DEBUG         Set to 1 for a debug build
#   BINDIR        可执行文件输出目录（默认：.）
#
# AVX-512 feature flags (set individually or use AVX512_ALL=1 for all):
#   AVX512_ALL        Enable all AVX-512 sieve paths
#   AVX512_ECM        AVX-512 ECM
#   AVX512_LASETUP    AVX-512 lasieve setup
#   AVX512_LASCHED    AVX-512 scheduler  (also forwarded to asm/)
#   AVX512_TD         AVX-512 trial division  (ICC only)
#   AVX512_TDSCHED    AVX-512 TD scheduler
#   AVX512_SIEVE1     AVX-512 sieve pass 1
#   AVX512_TDS0       AVX-512 TDS0
#   AVX512_SIEVE_SEARCH  AVX-512 sieve search
#   LASTATS           Gather statistics
#   MPQS              Enable MPQS
#
# Standalone usage examples:
#   make alle
#   make alle AVX512_ALL=1
#   make alle BINDIR=/some/output/dir
#   make alle GMP_INCDIR=/path/to/include GMP_LIBDIR=/path/to/lib
#   make clean
# -----------------------------------------------------------------------------


# --- Standalone fallbacks (overridden when called from top-level) ------------
CC         ?= gcc
CXX        ?= g++
CXX        ?= g++
CXX        ?= g++
BINDIR     ?= .

# GMP locations differ between distributions: Debian/Ubuntu keep gmp.h and
# libgmp.a under a multiarch tuple (/usr/include/<tuple> and /usr/lib/<tuple>)
# while most others use plain /usr/include together with /usr/lib or
# /usr/lib64.  Probe for the actual files rather than assuming one layout.
# An explicit GMP_INCDIR= / GMP_LIBDIR= on the command line -- which is what
# the top-level Makefile passes when it recurses into this one -- still wins.
MULTIARCH     := $(shell $(CC) -print-multiarch 2>/dev/null)
GMP_INC_PATHS := $(if $(MULTIARCH),/usr/include/$(MULTIARCH)) \
                 /usr/include /usr/local/include /opt/local/include
GMP_LIB_PATHS := $(if $(MULTIARCH),/usr/lib/$(MULTIARCH)) \
                 /usr/lib /usr/lib64 /usr/local/lib
find_dir = $(firstword $(foreach d,$(2),$(if $(wildcard $(d)/$(1)),$(d))))
GMP_INCDIR ?= $(call find_dir,gmp.h,$(GMP_INC_PATHS))
GMP_LIBDIR ?= $(call find_dir,libgmp.a,$(GMP_LIB_PATHS))

ifndef DETECTED_OS
    ifeq ($(OS),Windows_NT)
        DETECTED_OS := Windows
    else
        DETECTED_OS := $(shell uname -s)
    endif
endif

# Compiler family — mirrors the top-level detection so ifeq ($(CC),icc) checks
# work correctly even when CC is a full path or versioned name.
CC_BASENAME := $(notdir $(CC))

# C++：本子构建里的 .cpp 与主 Makefile 用同一套标准。
CXX_STD := -std=c++26

# C++：本子构建里的 .cpp 与主 Makefile 用同一套标准。
CXX_STD := -std=c++26

# C++：本子构建里的 .cpp 与主 Makefile 用同一套标准。
CXX_STD := -std=c++26
ifneq (,$(findstring icx,$(CC_BASENAME)))
    COMPILER_FAMILY := icc
else ifneq (,$(findstring icc,$(CC_BASENAME)))
    COMPILER_FAMILY := icc
else ifneq (,$(findstring clang,$(CC_BASENAME)))
    COMPILER_FAMILY := clang
else
    COMPILER_FAMILY := gcc
endif


# --- GMP static library ------------------------------------------------------
# Lasieve links statically against GMP so the sieve binaries are self-contained.
ifneq (,$(filter clean,$(MAKECMDGOALS)))
else ifeq (,$(GMP_LIBDIR))
    $(error libgmp.a not found in any of: $(GMP_LIB_PATHS).  Install libgmp-dev, \
            or pass GMP_LIBDIR=/path/to/lib)
else ifeq (,$(GMP_INCDIR))
    $(error gmp.h not found in any of: $(GMP_INC_PATHS).  Install libgmp-dev, \
            or pass GMP_INCDIR=/path/to/include)
endif
GMP_LIB := $(GMP_LIBDIR)/libgmp.a


# --- CFLAGS: platform and compiler selection ---------------------------------
#
# All three variants share a common base; platform and compiler add extras.
#
# gcc linux  : base flags only
# clang linux: base + -ffp-model=precise (required for reproducible FP results)
# mingw win  : base + -I. + -DULL_NO_UL -DBITS_PER_GMP_ULONG=32
#              (GMP's ./configure sets ulong=32-bit on Windows, but lasieve
#              assumes 64-bit; the defines reconcile this without changing
#              siever-config.h or the montgomery asm code)
#
CFLAGS_BASE := \
    -Wall -Wno-unused-variable -Wno-unused-function \
    -Ofast -fomit-frame-pointer -march=native -funroll-loops \
    -I$(GMP_INCDIR) -L$(GMP_LIBDIR) -I../../factor/shared/include -I../../factor/ecm/include -Iinclude -Iasm/include -fcommon

ifeq ($(DETECTED_OS),Windows)
    CFLAGS = -I. $(CFLAGS_BASE) -DULL_NO_UL -DBITS_PER_GMP_ULONG=32
else ifeq ($(COMPILER_FAMILY),clang)
    CFLAGS = $(CFLAGS_BASE) -ffp-model=precise
else
    CFLAGS = $(CFLAGS_BASE)
endif

ifeq ($(DEBUG),1)
    CFLAGS := $(filter-out -Ofast -fomit-frame-pointer -march=native -funroll-loops,$(CFLAGS))
    CFLAGS += -O0 -g
endif


# --- ASM_FLAGS: variables forwarded to the asm/ sub-make --------------------
# Always forward the compiler, GMP path, and OS so the asm Makefile doesn't
# need its own hardcoded values.  AVX512 flags are appended below as needed.
ASM_FLAGS := CC=$(CC) GMP_INCDIR=$(GMP_INCDIR) DETECTED_OS=$(DETECTED_OS) \
             $(if $(DEBUG),DEBUG=$(DEBUG))


# --- AVX-512 feature flags ---------------------------------------------------
# Individual flags can be set on the command line or in config.mk (via yafu's
# top-level Makefile which passes AVX512_ALL=1 when USE_AVX512=1 is set).
# AVX512_TD is ICC-only: that compiler generates faster AVX-512 TD code.

ifeq ($(AVX512_ECM),1)
    CFLAGS += -DAVX512_ECM
endif

ifeq ($(AVX512_LASETUP),1)
    CFLAGS += -DAVX512_LASIEVE_SETUP
endif

ifeq ($(AVX512_LASCHED),1)
    CFLAGS += -DAVX512_LASCHED
    ASM_FLAGS += AVX512_LASCHED=1
endif

ifeq ($(AVX512_TD),1)
    ifeq ($(COMPILER_FAMILY),icc)
        CFLAGS    += -DAVX512_TD
        ASM_FLAGS += AVX512_TD=1
    endif
endif

ifeq ($(AVX512_TDSCHED),1)
    CFLAGS += -DAVX512_TDSCHED
endif

ifeq ($(AVX512_SIEVE1),1)
    CFLAGS += -DAVX512_SIEVE1
endif

ifeq ($(AVX512_TDS0),1)
    CFLAGS += -DAVX512_TDS0
endif

ifeq ($(AVX512_SIEVE_SEARCH),1)
    CFLAGS += -DAVX512_SIEVE_SEARCH
endif

ifeq ($(LASTATS),1)
    CFLAGS += -DGATHER_STAT
endif

ifeq ($(MPQS),1)
    CFLAGS += -DGGNFS_MPQS
endif

ifeq ($(AVX512_ALL),1)
    ifeq ($(COMPILER_FAMILY),icc)
        CFLAGS    += -DUSE_AVX512F -DAVX512_LASIEVE_SETUP -DAVX512_LASCHED \
                     -DAVX512_TD -DAVX512_ECM -DAVX512_SIEVE1 \
                     -DAVX512_SIEVE_SEARCH -DAVX512_TDS0 -DAVX512_TDSCHED
        ASM_FLAGS += AVX512_TD=1
    else
        # gcc / clang: all AVX-512 paths except TD (which requires ICC codegen)
        CFLAGS    += -DAVX512_LASIEVE_SETUP -DAVX512_ECM -DAVX512_LASCHED \
                     -DAVX512_SIEVE1 -DAVX512_SIEVE_SEARCH \
                     -DAVX512_TDS0 -DAVX512_TDSCHED
    endif
    ASM_FLAGS += AVX512_LASCHED=1
endif


# --- Build rules -------------------------------------------------------------

.SUFFIXES:
.SECONDARY: *.cpp *.o *.a

ASMDIRS = asm

list_asm_files = \
    $(foreach file,$(shell $(MAKE) -s -C $(asm_dir) bup),$(asm_dir)/$(file))

ASMFILES = $(foreach asm_dir,$(ASMDIRS),$(list_asm_files))

# Delegate any asm/* target to the asm sub-make, forwarding ASM_FLAGS.
asm/%: force
	$(MAKE) $(ASM_FLAGS) -C asm $*

# Generic compile rules
%.o: %.c include/if.h asm/include/siever-config.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp include/if.h asm/include/siever-config.h
	$(CXX) $(CFLAGS) $(CXX_STD) -c -o $@ $<

%.o: %.s
	$(CC) $(CFLAGS) -c $^

.PHONY: force alle allf allg clean


# --- Targets -----------------------------------------------------------------

# BINDIR 为 . 时保持原目标名；其他值作为最终可执行文件的目录前缀。
BINPREFIX := $(if $(filter-out . ./,$(strip $(BINDIR))),$(patsubst %/,%,$(strip $(BINDIR)))/,)
# the six I values are no longer six executables; see "One binary" above
SIEVE_E_TARGETS :=
SIEVE_F_TARGETS :=
SIEVE_G_TARGETS :=





# --- Explicit header dependencies --------------------------------------------

fbgen.o: include/gmp-aux.h
fbgen.o lasieve-prepn.o: asm/include/32bit.h
redu2.o: include/redu2.h
primgen32.o fbgen.o: include/primgen32.h
gnfs-lasieve4e.cpp: la-cs.cpp
lasieve-prepn.o: include/lasieve-prepn.h include/recurrence6.h
mpqs3.o: asm/include/mpqs-config.h


# ----------------------------------------------------------------------------- 
# One binary, six I values
#
# I is a compile-time constant, so the six I values used to be six
# executables: the six liblasieveI%d.a libraries define the same names and ld
# refuses to link two of them.  Every source is now compiled once per I with
# the colliding names suffixed, all six sets go into one image, and
# lasieve_dispatch.cpp picks between them from the first argument.
#
# The per-I copies are not only about collisions.  Each I value ends up with a
# private factor base, montgomery state, ECM/P-1 cache and strategy counters,
# which is what lets two sievers run in one process.  one
# -Dlasieve_ns=lasieve_I<N> per translation unit is the whole of the
# mechanism now, and the namespaces carry the reasoning.
# -----------------------------------------------------------------------------

I_VALUES := 11 12 13 14 15 16

SIEVER_SRCS := \
	gnfs-lasieve4e.cpp if.cpp input-poly.cpp \
	redu2.cpp fbgen.cpp fbgen64.cpp \
	real-poly-aux.cpp mpqs.cpp primgen32.cpp \
	lasieve-prepn.cpp pprime_p.cpp strategy.cpp \
	ecm.cpp mpqs3.cpp pm1.cpp \
	microecm.cpp micropm1.cpp tinyecm.cpp \
	recurrence6.cpp

define SIEVER_IRULES
I$(1)_OBJS := $$(foreach s,$$(SIEVER_SRCS),objI$(1)/$$(basename $$s).o)

objI$(1)/%.o: %.c
	@mkdir -p objI$(1)
	$$(CC) $$(CFLAGS) -DI_bits=$(1) \
		-Dlasieve_ns=lasieve_I$(1) -c -o $$@ $$<

objI$(1)/%.o: %.cpp
	@mkdir -p objI$(1)
	$$(CXX) $$(CFLAGS) $$(CXX_STD) -DI_bits=$(1) \
		-Dlasieve_ns=lasieve_I$(1) -c -o $$@ $$<
endef

$(foreach i,$(I_VALUES),$(eval $(call SIEVER_IRULES,$(i))))

ALL_OBJS := $(foreach i,$(I_VALUES),$(I$(i)_OBJS))
ALL_ASM  := $(foreach i,$(I_VALUES),asm/libcoreI$(i).a asm/liblasieveI$(i).a)

# shared, and none of its symbols collide between I values
lasieve_dispatch.o: lasieve_dispatch.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

# Also linked by the standalone sieve regressions, which compile if.cpp and
# redu2.cpp directly and so need lasieve_bail() without the whole dispatch.
lasieve_bail.o: lasieve_bail.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

lasieve_ctx.o: lasieve_ctx.cpp include/lasieve_ctx.h | include/lasieve_ctx.h
	$(CC) $(CFLAGS) -c -o $@ $<

libgmp-aux.a: gmp-aux.o mpz-ull.o
	$(AR) rcs $@ $^

# Everything the parent Makefile has to link in, in one place.
#
# The hand-written assembly is full of 32-bit absolute relocations (pt64.o
# alone has one against the factor-base inverse table), which a PIE link
# rejects: it wants a copy relocation.  The assembly is not PIC, so the parent
# links yafu with -no-pie for this reason.
LASIEVE_OBJS := lasieve_dispatch.o lasieve_bail.o lasieve_ctx.o $(ALL_OBJS)
LASIEVE_LIBS := libgmp-aux.a $(ALL_ASM)

objects: $(LASIEVE_OBJS) $(LASIEVE_LIBS)

# The parent Makefile pulls the list in with $(shell $(MAKE) -s -C ... bobs) and
# turns each entry into a dependency, so that building yafu builds the siever.
bobs:
	@for f in $(LASIEVE_OBJS) $(LASIEVE_LIBS); do echo $$f; done

# A standalone driver for the sieve tests, built on demand and never part of
# `make all`: the shipped artefact is yafu, with the siever inside it.  The
# test needs to run the siever by itself to compare relation output.
$(BINPREFIX)check_sieve: $(LASIEVE_OBJS) $(LASIEVE_LIBS) check_sieve.o
	$(CC) $(CFLAGS) -no-pie -o $@ $^ $(GMP_LIB) -lm

check_sieve.o: check_sieve.cpp lasieve_dispatch.cpp include/lasieve_bail.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The six I values at once, one thread each.  sieve_oracle.sh runs them one after
# another and so cannot see cross-talk between two sievers sharing a process,
# which is exactly what the per-I objects exist to prevent.
$(BINPREFIX)check_sieve_mt: $(LASIEVE_OBJS) $(LASIEVE_LIBS) check_sieve_mt.o
	$(CC) $(CFLAGS) -no-pie -o $@ $^ $(GMP_LIB) -lm -lpthread

check_sieve_mt.o: check_sieve_mt.cpp lasieve_dispatch.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

alle: objects

allf allg:
	@echo "only the 'e' variant builds in this tree"
	@false

# --- Test / stat targets -----------------------------------------------------

mpqsz.o: mpqs.cpp asm/include/mpqs-config.h
	$(CC) $(CFLAGS) -O2 -DMPQS_STAT -DMPQS_ZEIT -c -o $@ $<

mpqstest: mpqstest.o mpqsz.o lasieve_bail.o if.o mpz-ull.o asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

mpqs3z.o: mpqs3.cpp asm/include/mpqs-config.h
	$(CC) $(CFLAGS) -O2 -DMPQS3_STAT -DMPQS3_ZEIT -c -o $@ $<

mpqs3test: mpqs3test.o mpqs3z.o lasieve_bail.o if.o lasieve_ctx.o libgmp-aux.a asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

mpqsstat: mpqsstat.o mpqs.o mpqs3.o lasieve_bail.o if.o lasieve_ctx.o mpz-ull.o libgmp-aux.a asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

# 多实例验证：两个实例的 ECM/PM1 状态必须互不干扰
ctx_test.o: ctx_test.cpp include/lasieve_ctx.h include/ecm.h include/pm1.h \
	include/if.h asm/include/siever-config.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINPREFIX)ctx_test: ctx_test.o lasieve_ctx.o lasieve_bail.o if.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

# ECM/P-1 的数值正确性，以及两个实例交错推进的隔离性
ecm_pm1_test.o: ecm_pm1_test.cpp include/lasieve_ctx.h include/ecm.h include/pm1.h \
	include/if.h asm/include/siever-config.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINPREFIX)ecm_pm1_test: ecm_pm1_test.o ecm.o pm1.o lasieve_ctx.o lasieve_bail.o if.o asm/liblasieve.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm


ecmz.o: ecm.cpp asm/include/siever-config.h
	$(CC) $(CFLAGS) -DECM_STAT -DECM_ZEIT -c -o $@ $<

ecmtest: ecmtest.o ecmz.o lasieve_bail.o if.o lasieve_ctx.o asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

ecmstat: ecmstat.o ecmz.o lasieve_bail.o if.o lasieve_ctx.o asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

pm1z.o: pm1.cpp asm/include/siever-config.h
	$(CC) $(CFLAGS) -DPM1_STAT -DPM1_ZEIT -c -o $@ $<

pm1test: pm1test.o pm1z.o lasieve_bail.o if.o lasieve_ctx.o asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm

pm1stat: pm1stat.o pm1z.o lasieve_bail.o if.o lasieve_ctx.o asm/liblasieve.a
	$(CC) $(CFLAGS) -o $@ $^ $(GMP_LIB) -lm


# --- Clean -------------------------------------------------------------------

clean:
	-rm -rf objI11 objI12 objI13 objI14 objI15 objI16
	-rm -rf asm/coreI11 asm/coreI12 asm/coreI13 asm/coreI14 asm/coreI15 asm/coreI16
	-rm -f asm/libcoreI11.a asm/libcoreI12.a asm/libcoreI13.a \
		asm/libcoreI14.a asm/libcoreI15.a asm/libcoreI16.a
	-rm -f *.o *.a asm/*.o asm/*.a asm/*.s asm/*.S \
		$(BINPREFIX)ctx_test $(BINPREFIX)ecm_pm1_test $(BINPREFIX)check_sieve \
		$(BINPREFIX)check_sieve_mt
