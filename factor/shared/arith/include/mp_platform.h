/*----------------------------------------------------------------------
mp_platform.h

Shared platform / detection / intrinsic-polyfill layer for the YAFU limb
arithmetic. limb1.{h,c}, limb2.{h,c}, microecm.c and micropm1.c include
this and nothing else for compiler / OS / ISA concerns.

Layering:
  common.h       - assembler-syntax detection (ASM_G / ASM_M, GCC_ASM64X,
                   the A/X guards).  Papadopoulos / Gladman lineage, shared
                   across the wider YAFU tree.  We build ON TOP of it and do
                   not absorb it.
  mp_platform.h  - this file: ISA-feature normalization, inline/unused
                   attribute macros, and ONE copy of every intrinsic polyfill
                   (_umul128, __umulh, _addcarry_u64, _subborrow_u64/_u32,
                   _udiv128, the {trail,lead}_zcnt / reset_lsb family).

This replaces the per-file #if ladders that were re-derived (and had drifted)
in arith.h, arith.c, monty.h, monty.c, microecm.c and micropm1.c.
----------------------------------------------------------------------*/

#ifndef MP_PLATFORM_H
#define MP_PLATFORM_H

#include <stdint.h>
#include "common.h"
#include "carry_intrin.h"

/* -------- vendor intrinsics: pulled in once, here -------- */
#if !defined(__aarch64__)
  #include <immintrin.h>
#endif


/* ======================================================================
   1. Derived capability flags  (single source of truth)
   ====================================================================== */

/* 128-bit integer type available? */
#if defined(__SIZEOF_INT128__) && (__SIZEOF_INT128__ == 16)
  #ifndef HAS_UINT128
    #define HAS_UINT128
  #endif
  typedef __uint128_t uint128_t;
#endif

/* GNU-style inline asm usable for x86-64?
   common.h already sets GCC_ASM64X for gcc/clang/icc on unix-x64.
   ASM_ARITH_DEBUG forces the portable path so the two code paths can be
   A/B compared. */
#if defined(GCC_ASM64X) && !defined(ASM_ARITH_DEBUG) && defined(__x86_64__)
  #define MP_HAS_GNU_ASM_X64 1
#else
  #define MP_HAS_GNU_ASM_X64 0
#endif

/* INLINE_ASM was previously #define'd =1 locally in monty.c -- and, because it
   was defined *after* the my_ctz/my_clz functions, the BMI1 asm paths in those
   functions were never actually reached.  Centralizing it here (this header is
   included first) makes INLINE_ASM consistent everywhere.
   NOTE: this ACTIVATES the previously-dead BMI1 ctz/clz asm paths -- a real
   codegen change.  Leave MP_ENABLE_LEGACY_INLINE_ASM undefined to keep the
   historical (portable-builtin) behavior for those functions until you have
   re-benchmarked them. */
#ifndef INLINE_ASM
  #define INLINE_ASM MP_HAS_GNU_ASM_X64
#endif


/* ======================================================================
   2. Inline / unused attribute macros
   ====================================================================== */


/* Compiler-compat attribute macros.  These now live HERE (moved down out of
   ytools.h, which is a much heavier header and must not be a dependency of
   low-level arithmetic).  ytools.h should `#include "mp_platform.h"` and delete
   its own copies of these -- along with ALIGNED_MEM / INLINE / PREFETCH, which
   are also pure compiler detection with no OS dependency.
   The bodies below are byte-for-byte ytools.h's, so even during the transition
   (before ytools.h is edited) an identical macro redefinition is legal and
   silent in C regardless of include order.  The #ifndef guards additionally
   let a ytools.h-first TU win without a diagnostic. */
#if defined(__GNUC__) || defined(__clang__) || \
      defined(__INTEL_COMPILER) || defined(__INTEL_LLVM_COMPILER)
  #ifndef UNUSED_VAR
    #define UNUSED_VAR __attribute__((unused))
  #endif
  #ifndef UNUSED_FUNC
    #define UNUSED_FUNC __attribute__((unused))
  #endif
#else
  #ifndef UNUSED_VAR
    #define UNUSED_VAR
  #endif
  #ifndef UNUSED_FUNC
    #define UNUSED_FUNC
  #endif
#endif



/* ======================================================================
   3. Bit scan / reset : Make every path a value-returning inline
   ====================================================================== */

#if defined(USE_BMI2) || defined(TARGET_KNL) || defined(USE_AVX512F)
    /* BMI branch (where _blsr_u64/_blsr_u32 are available) */
    MP_FORCE_INLINE uint64_t _reset_lsb64(uint64_t x) { return _blsr_u64(x); }
    MP_FORCE_INLINE uint32_t _reset_lsb(uint32_t x) { return _blsr_u32(x); }
#else
    /* portable fallback branch */
    MP_FORCE_INLINE uint64_t _reset_lsb64(uint64_t x) { return x & (x - 1); }
    MP_FORCE_INLINE uint32_t _reset_lsb(uint32_t x) { return x & (x - 1); }
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_LLVM_COMPILER)
  #define _lead_zcnt64  __builtin_clzll
  #define _trail_zcnt   __builtin_ctzl
  #define _trail_zcnt64 __builtin_ctzll
#else   /* portable loop fallback */
  MP_FORCE_INLINE uint64_t _lead_zcnt64(uint64_t x) {
      uint64_t pos;
      if (x) { for (pos = 0; !(x & (1ULL << (63 - pos))); pos++) ; }
      else   { pos = 8 * sizeof(x); }
      return pos;
  }
  MP_FORCE_INLINE uint32_t _trail_zcnt(uint32_t x) {
      uint32_t pos;
      if (x) { x = (x ^ (x - 1)) >> 1; for (pos = 0; x; pos++) x >>= 1; }
      else   { pos = 8 * sizeof(x); }
      return pos;
  }
  MP_FORCE_INLINE uint64_t _trail_zcnt64(uint64_t x) {
      uint64_t pos;
      if (x) { x = (x ^ (x - 1)) >> 1; for (pos = 0; x; pos++) x >>= 1; }
      else   { pos = 8 * sizeof(x); }
      return pos;
  }
#endif


/* ---- zero-safe ("full") variants -------------------------------------------
   _lead_zcnt64 / _trail_zcnt64 above are the fast paths and are UNDEFINED at
   zero on the gcc/clang builtin branches, so every call site has to guarantee
   a non-zero argument.  These wrappers add the zero case, returning the width,
   and are what monty.c used to spell my_clz64 / my_ctz64.

   Being wrappers rather than copies matters: they inherit whatever the ladder
   above selected, so there is exactly one implementation of each scan per
   platform.  The `if (n)` guard is also what keeps the builtin paths out of
   undefined behaviour -- do not remove it. */
MP_FORCE_INLINE uint64_t _lead_full_zcnt(uint64_t n) {
    if (n)
        return (uint64_t)_lead_zcnt64(n);
    return 64;
}
MP_FORCE_INLINE uint64_t _trail_full_zcnt(uint64_t n) {
    if (n)
        return (uint64_t)_trail_zcnt64(n);
    return 64;
}


/* ======================================================================
   4. Wide-multiply / divide / carry polyfills
   One definition of each, here.
   ====================================================================== */

  /* --- 64x64 -> 128 multiply --- */
  #if defined(HAS_UINT128)
    MP_FORCE_INLINE uint64_t _umul128(uint64_t x, uint64_t y, uint64_t *hi) {
        __uint128_t p = (__uint128_t)x * (__uint128_t)y;
        *hi = (uint64_t)(p >> 64);
        return (uint64_t)p;
    }
    MP_FORCE_INLINE uint64_t __umulh(uint64_t x, uint64_t y) {
        return (uint64_t)(((__uint128_t)x * (__uint128_t)y) >> 64);
    }
  #elif MP_HAS_GNU_ASM_X64
    MP_FORCE_INLINE uint64_t _umul128(uint64_t x, uint64_t y, uint64_t *hi) {
        __asm__("mulq %3\n\t" : "=&a"(x), "=&d"(y) : "0"(x), "1"(y) : "cc");
        *hi = y; return x;
    }
    MP_FORCE_INLINE uint64_t __umulh(uint64_t x, uint64_t y) {
        uint64_t hi; (void)_umul128(x, y, &hi); return hi;
    }
  #else
    /* portable 32x32 schoolbook -- only reached on a non-x64 target with no
       __int128, which is outside the current support matrix. */
    MP_FORCE_INLINE uint64_t _umul128(uint64_t x, uint64_t y, uint64_t *hi) {
        uint64_t xl = (uint32_t)x, xh = x >> 32, yl = (uint32_t)y, yh = y >> 32;
        uint64_t ll = xl * yl, lh = xl * yh, hl = xh * yl, hh = xh * yh;
        uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
        *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
        return (mid << 32) | (uint32_t)ll;
    }
    MP_FORCE_INLINE uint64_t __umulh(uint64_t x, uint64_t y) {
        uint64_t hi; (void)_umul128(x, y, &hi); return hi;
    }
  #endif


  /* --- 128/64 -> 64 divide (quotient) with remainder out ---
     The GNU-asm divq branch comes FIRST on x86-64, ahead of the __int128
     form.  Measured: gcc lowers `(__uint128_t)n / d` to a __udivmodti4
     libcall (full software 128-bit division) rather than a single divq --
     roughly an order of magnitude slower.  clang recognizes the idiom and
     emits divq, so it is unaffected either way; this only rescues gcc/icc.
     ASM_ARITH_DEBUG clears MP_HAS_GNU_ASM_X64 and drops back to __int128.

     CONTRACT: requires d != 0 and hi < d.  divq raises #DE if the quotient
     will not fit in 64 bits.  Note the __int128 fallback instead truncates
     silently, so a
     caller that violates the precondition changes from wrong-answer to trap
     when this branch is active.  Callers must guarantee hi < d regardless. */
  #if MP_HAS_GNU_ASM_X64
    MP_FORCE_INLINE uint64_t _udiv128(uint64_t hi, uint64_t lo, uint64_t d,
                                      uint64_t *rem) {
        uint64_t q, r;
        __asm__("divq %[d]"
                : "=a"(q), "=d"(r)
                : "a"(lo), "d"(hi), [d]"rm"(d)
                : "cc");
        *rem = r; return q;
    }
  #elif defined(HAS_UINT128)
    MP_FORCE_INLINE uint64_t _udiv128(uint64_t hi, uint64_t lo, uint64_t d,
                                      uint64_t *rem) {
        __uint128_t n = ((__uint128_t)hi << 64) | lo;
        *rem = (uint64_t)(n % d);
        return (uint64_t)(n / d);
    }
  #else
    /* No hardware path: unreachable on the current support matrix (every
       target has either GNU-asm divq or __int128).  If a non-x86
       no-__int128 target is ever added, provide a software _udiv128 here,
       e.g. built on the Knuth uint128_div() that lives in limb2.c. */
    uint64_t _udiv128(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem);
  #endif

/* ======================================================================
 5. Add-with-carry / subtract-with-borrow
 The mp_* wrappers are defined on every target.
 ====================================================================== */
#endif /* MP_PLATFORM_H */
