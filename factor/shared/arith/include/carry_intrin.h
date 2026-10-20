/*----------------------------------------------------------------------
64 位带进位/借位的原语。

这一组以前在四个文件里各抄了一份宏（arith.cpp、monty.h、microecm.cpp、
micropm1.cpp），抄的是不同编译器内建，约定却不一致：

    MSVC 的 _addcarry_u64(c_in, a, b, out)   返回进位，out 写和
    GCC 的 __builtin_addcll(a, b, c_in, out) 返回和，out 写进位

宏名一样、约定相反，调用点按哪种约定写就决定了它对不对。arith.cpp、
microecm.cpp、micropm1.cpp 的调用点按 MSVC 约定写，monty.h / limb1.h 的
mulredc 那几份也按 MSVC 约定写，可这几份文件里定义的是 GCC 的 builtin
宏 —— 可移植分支上算出来的和与进位是反的，uecm_submod / upm1_submod 判的
是"差是不是非零"而不是"有没有借位"。

现在统一到这一组函数上：约定一律是"返回进位/借位，out 写和/差"，实现按
"原生内建 → clang builtin → 可移植 C"分三档。调用点只要把名字换掉，形状
不用动。

从 mp_platform.h 拆出来单独成头，是因为 mp_platform.h 里还有 _umul128 /
_udiv128 / _reset_lsb / _lead_zcnt64 这一组，它们和 arith.h、arith.cpp 自己
定义的那几份正面冲突（宏与函数、形态还不一样），不能被那几个文件连带引入。
----------------------------------------------------------------------*/

#ifndef MP_CARRY_INTRIN_H
#define MP_CARRY_INTRIN_H

#include <stdint.h>

/* 原生那一档用到的 _addcarry_u64 / _subborrow_u64 之类在 adxintrin.h 里，
   由 immintrin.h 带进来。 */
#ifndef __aarch64__
#include <immintrin.h>
#endif

/* 第三档（__int128 加法 + 可移植 C 减法）要用 __int128。arith.h 那边也定义
   一个同名宏，这里补上，免得没引 arith.h 的文件少一档实现。 */
#if !defined(HAS_UINT128) && defined(__SIZEOF_INT128__) && (__SIZEOF_INT128__ == 16)
#define HAS_UINT128
#endif

/* MP_FORCE_INLINE expands to include `static`.  This is deliberate: the old
   monty.h modular block (submod/addmod/mulredc/sqrredc/...) was declared plain
   `__inline` with external linkage, which is a one-definition-rule landmine
   when a header is included by multiple TUs (gnu89-inline vs C99 disagree about
   who emits the external symbol).  Routing every header-resident kernel
   through this macro makes them all `static inline` and removes the hazard. */
#if !defined(MP_FORCE_INLINE)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER) \
   || defined(__INTEL_LLVM_COMPILER)
  #define MP_FORCE_INLINE static inline __attribute__((always_inline))
#else
  #define MP_FORCE_INLINE static inline
#endif
#endif

/* Carry/borrow return type.  The _addcarry_u64 / _subborrow_u64 provided here
   (the x86 intrinsic, or the portable wrappers further down) uniformly follow
   the same convention: they RETURN the carry/borrow and write the result
   through the out pointer.  A single unsigned char is therefore correct on
   every compiler; use mp_carry_t in place of the ad-hoc `rettype` that appeared
   in monty.h / microecm.c / arith.c. */
typedef unsigned char mp_carry_t;

/* --- add-carry / sub-borrow -------------------------------------------
   Canonical API (the Intel convention, used on every target here): RETURN the
   carry/borrow, write the low result through `out`.

       mp_carry_t mp_addcarry_u64 (mp_carry_t c_in, uint64_t a, uint64_t b, uint64_t *out);
       mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a, uint64_t b, uint64_t *out);

   These are performance-critical, so we use the best mechanism available
   rather than a portable emulation:

     1. native _addcarry_u64/_subborrow_u64 -- gcc/clang/icc on x86 via
        <immintrin.h> (adxintrin.h).  Lowers to a
        single adc/sbb and chains carries without materializing flags.
     2. clang __builtin_addcll/__builtin_subcll -- carry-in AND carry-out in
        one builtin; used on non-x86 clang (e.g. aarch64).
     3. __int128 for add, plain C for sub -- used by gcc < 10, which has no
        native _addcarry_u64.  Mixed on purpose; see the table there.
     4. gcc __builtin_add_overflow/__builtin_sub_overflow -- no carry-in, so
        composed from two ops.
     5. portable C fallback.

   NOTE: the wrappers are named mp_* deliberately.  Defining our own
   _addcarry_u64 is a *conflicting redefinition* on x86 gcc/clang, because
   adxintrin.h already declares it (verified: gcc 13 errors out).  The
   compatibility aliases below are only installed when no native version
   exists.  Taking uint64_t* in our own signature (and bouncing through a
   temporary for the native call) also removes the LP64 incompatible-pointer
   warning at call sites, where uint64_t is `unsigned long` but gcc/clang
   declare the intrinsic with `unsigned long long*`.  See the note on the
   native branch for why that temporary is passed as void*.  It costs
   nothing at -O1 and up.

   Define MP_NO_NATIVE_ADDCARRY to force the builtin path (for benchmarking),
   or MP_FORCE_PORTABLE_CARRY to force the plain-C path. */

#if !defined(MP_HAS_NATIVE_ADDCARRY)
  #if defined(MP_FORCE_PORTABLE_CARRY) || defined(MP_NO_NATIVE_ADDCARRY)
    #define MP_HAS_NATIVE_ADDCARRY 0
  #elif defined(MP_ASSUME_NATIVE_ADDCARRY)
    /* escape hatch: force the native path on for a toolchain that has the
       intrinsics but is not recognized by the tests below. */
    #define MP_HAS_NATIVE_ADDCARRY 1
  #elif defined(__x86_64__) && \
        (defined(__clang__) || defined(__INTEL_COMPILER) || \
         defined(__INTEL_LLVM_COMPILER))
    #define MP_HAS_NATIVE_ADDCARRY 1
  #elif defined(__x86_64__) && defined(__GNUC__) && (__GNUC__ >= 10)
    /* VERSION GATE -- do not loosen without testing.  gcc's adxintrin.h has
       not always provided the non-`x` _addcarry_u64 / _subborrow_u64: on
       gcc 8.5 they are absent, and relying on them produces an implicit
       declaration (assumed `extern int`) that compiles with only a warning
       and then fails at LINK with "undefined reference to _addcarry_u64".
       They are NOT ADX-gated, so -madx does not help -- it is purely a
       header-vintage issue, hence a version test rather than a target test.
       gcc >= 10 is known good (12.2 verified); 8.5 verified missing.
       gcc 9 and below fall through to the mixed __int128/plain-C tier
       below.  That tier is measurably slower than native -- on gcc 12.2,
       0.68/0.75 vs 0.52/0.51 ns per limb -- so this gate costs real
       performance on old gcc and should be lowered if you can confirm
       your version has the intrinsics (see MP_ASSUME_NATIVE_ADDCARRY).
       If your older gcc does have them, define MP_ASSUME_NATIVE_ADDCARRY. */
    #define MP_HAS_NATIVE_ADDCARRY 1
  #else
    #define MP_HAS_NATIVE_ADDCARRY 0
  #endif
#endif

#if MP_HAS_NATIVE_ADDCARRY
  #define MP_CARRY_IMPL "native _addcarry_u64/_subborrow_u64"

  /* The out-parameter type of these intrinsics is NOT consistent across
     compilers, and there is no single typed temporary that satisfies all of
     them:

       gcc / clang         unsigned long long *
       icc classic (LP64)  unsigned __int64 *  ==  unsigned long *

     Passing `unsigned long long *` gets icc warning #167; passing uint64_t *
     gets -Wincompatible-pointer-types on LP64 gcc/clang.  A `void *` satisfies
     every one of them: in C it implicitly converts to whatever object pointer
     type the declaration actually uses, so we no longer have to track each
     compiler's header details -- which is twice now that guessing has cost us.
     The union makes the punning well-defined rather than an aliasing gamble.
     Verified to produce the same adc/sbb chain, with no spill, as the plain
     temporary did -- the address never escapes, so it stays in a register.

     C++ does NOT implicitly convert void*, so MP_INTRIN_OUT falls back to a
     typed pointer there.  The tree is all .cpp now, so the C branch is only
     kept for the benefit of any future C translation unit. */
  typedef union { uint64_t u64; unsigned long long ull; unsigned long ul; }
      mp_u64_slot_t;
  typedef union { uint32_t u32; unsigned int ui; unsigned long ul; }
      mp_u32_slot_t;

  #if defined(__cplusplus)
    #define MP_INTRIN_OUT64(slot) (&(slot).ull)
    #define MP_INTRIN_OUT32(slot) (&(slot).ui)
  #else
    #define MP_INTRIN_OUT64(slot) ((void *)&(slot))
    #define MP_INTRIN_OUT32(slot) ((void *)&(slot))
  #endif

  MP_FORCE_INLINE mp_carry_t mp_addcarry_u64(mp_carry_t c_in, uint64_t a,
                                             uint64_t b, uint64_t *out) {
      mp_u64_slot_t t;
      mp_carry_t c = (mp_carry_t)_addcarry_u64((unsigned char)c_in, a, b,
                                               MP_INTRIN_OUT64(t));
      *out = t.u64;
      return c;
  }
  MP_FORCE_INLINE mp_carry_t mp_addcarry_u32(mp_carry_t c_in, uint32_t a,
      uint32_t b, uint32_t* out) {
      mp_u32_slot_t t;
      mp_carry_t c = (mp_carry_t)_addcarry_u32((unsigned char)c_in, a, b,
          MP_INTRIN_OUT32(t));
      *out = t.u32;
      return c;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a,
                                              uint64_t b, uint64_t *out) {
      mp_u64_slot_t t;
      mp_carry_t br = (mp_carry_t)_subborrow_u64((unsigned char)b_in, a, b,
                                                 MP_INTRIN_OUT64(t));
      *out = t.u64;
      return br;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u32(mp_carry_t b_in, uint32_t a,
                                              uint32_t b, uint32_t *out) {
      mp_u32_slot_t t;
      mp_carry_t br = (mp_carry_t)_subborrow_u32((unsigned char)b_in, a, b,
                                                 MP_INTRIN_OUT32(t));
      *out = t.u32;
      return br;
  }

#elif defined(__clang__) && !defined(MP_FORCE_PORTABLE_CARRY)
  #define MP_CARRY_IMPL "clang __builtin_addcll/__builtin_subcll"

  /* __builtin_addcll RETURNS the sum and writes the carry-out through the
     pointer -- the opposite of the convention above, so do not cross them. */
  MP_FORCE_INLINE mp_carry_t mp_addcarry_u64(mp_carry_t c_in, uint64_t a,
                                             uint64_t b, uint64_t *out) {
      unsigned long long carry_out;
      *out = (uint64_t)__builtin_addcll((unsigned long long)a,
                                        (unsigned long long)b,
                                        (unsigned long long)c_in, &carry_out);
      return (mp_carry_t)carry_out;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a,
                                              uint64_t b, uint64_t *out) {
      unsigned long long borrow_out;
      *out = (uint64_t)__builtin_subcll((unsigned long long)a,
                                        (unsigned long long)b,
                                        (unsigned long long)b_in, &borrow_out);
      return (mp_carry_t)borrow_out;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u32(mp_carry_t b_in, uint32_t a,
                                              uint32_t b, uint32_t *out) {
      unsigned int borrow_out;
      *out = (uint32_t)__builtin_subc(a, b, (unsigned int)b_in, &borrow_out);
      return (mp_carry_t)borrow_out;
  }

#elif defined(HAS_UINT128) && !defined(MP_FORCE_PORTABLE_CARRY)
  #define MP_CARRY_IMPL "__int128 add + portable-C sub"

  /* Fallback wherever __int128 exists -- in practice gcc < 10, which has no
     native _addcarry_u64.  Deliberately MIXED, because the two directions do
     not behave the same way.

     The 128-bit add lowers well (carry-out is just the high word), but the
     128-bit subtract does not: extracting the borrow as (t >> 64) & 1 defeats
     the compiler, and the plain-C borrow idiom beats it badly.  Measured on
     AMD, ns per limb over an 8-limb chain:

                        gcc 8.5          clang 17        gcc 12.2
       add  __int128      0.675            0.484           0.677
       add  plain C       1.084            0.887           0.741   -> __int128 wins
       sub  __int128      1.507            0.546           0.753
       sub  plain C       0.795            0.267           0.795   -> plain C wins

     Using __int128 for both would make subtract ~2x SLOWER than plain C on
     gcc 8.5 and clang.  So: __int128 for add, plain C for sub.  On gcc 12.2
     the two are within noise, so the split costs nothing there.

     For reference, the native intrinsic path (tier 1) beats everything here
     on every compiler tested -- gcc 12.2 0.516/0.511, clang 0.265/0.267 --
     so this tier only ever runs where tier 1 is unavailable. */
  MP_FORCE_INLINE mp_carry_t mp_addcarry_u64(mp_carry_t c_in, uint64_t a,
                                             uint64_t b, uint64_t *out) {
      __uint128_t t = (__uint128_t)a + (__uint128_t)b + (__uint128_t)c_in;
      *out = (uint64_t)t;
      return (mp_carry_t)(t >> 64);
  }
  /* plain C, NOT __int128 -- see the table above */
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a,
                                              uint64_t b, uint64_t *out) {
      uint64_t d  = a - b;
      mp_carry_t br = (mp_carry_t)(a < b);
      uint64_t d2 = d - (uint64_t)b_in;
      br |= (mp_carry_t)(d < (uint64_t)b_in);
      *out = d2;
      return br;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u32(mp_carry_t b_in, uint32_t a,
                                              uint32_t b, uint32_t *out) {
      uint32_t d  = a - b;
      mp_carry_t br = (mp_carry_t)(a < b);
      uint32_t d2 = d - (uint32_t)b_in;
      br |= (mp_carry_t)(d < (uint32_t)b_in);
      *out = d2;
      return br;
  }

#elif defined(__GNUC__) && !defined(MP_FORCE_PORTABLE_CARRY)
  #define MP_CARRY_IMPL "gcc __builtin_add_overflow/__builtin_sub_overflow"

  MP_FORCE_INLINE mp_carry_t mp_addcarry_u64(mp_carry_t c_in, uint64_t a,
                                             uint64_t b, uint64_t *out) {
      uint64_t s;
      mp_carry_t c1 = (mp_carry_t)__builtin_add_overflow(a, b, &s);
      mp_carry_t c2 = (mp_carry_t)__builtin_add_overflow(s, (uint64_t)c_in, &s);
      *out = s;
      return (mp_carry_t)(c1 | c2);   /* c1 and c2 are never both set */
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a,
                                              uint64_t b, uint64_t *out) {
      uint64_t d;
      mp_carry_t b1 = (mp_carry_t)__builtin_sub_overflow(a, b, &d);
      mp_carry_t b2 = (mp_carry_t)__builtin_sub_overflow(d, (uint64_t)b_in, &d);
      *out = d;
      return (mp_carry_t)(b1 | b2);
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u32(mp_carry_t b_in, uint32_t a,
                                              uint32_t b, uint32_t *out) {
      uint32_t d;
      mp_carry_t b1 = (mp_carry_t)__builtin_sub_overflow(a, b, &d);
      mp_carry_t b2 = (mp_carry_t)__builtin_sub_overflow(d, (uint32_t)b_in, &d);
      *out = d;
      return (mp_carry_t)(b1 | b2);
  }

#else
  #define MP_CARRY_IMPL "portable C"

  MP_FORCE_INLINE mp_carry_t mp_addcarry_u64(mp_carry_t c_in, uint64_t a,
                                             uint64_t b, uint64_t *out) {
      uint64_t s = a + (uint64_t)c_in;
      mp_carry_t c = (mp_carry_t)(s < a);
      s += b;
      c |= (mp_carry_t)(s < b);
      *out = s;
      return c;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u64(mp_carry_t b_in, uint64_t a,
                                              uint64_t b, uint64_t *out) {
      uint64_t d  = a - b;
      mp_carry_t br = (mp_carry_t)(a < b);
      uint64_t d2 = d - (uint64_t)b_in;
      br |= (mp_carry_t)(d < (uint64_t)b_in);
      *out = d2;
      return br;
  }
  MP_FORCE_INLINE mp_carry_t mp_subborrow_u32(mp_carry_t b_in, uint32_t a,
                                              uint32_t b, uint32_t *out) {
      uint32_t d  = a - b;
      mp_carry_t br = (mp_carry_t)(a < b);
      uint32_t d2 = d - (uint32_t)b_in;
      br |= (mp_carry_t)(d < (uint32_t)b_in);
      *out = d2;
      return br;
  }
#endif

/* Compatibility aliases for existing call sites.  Installed ONLY when the
   platform has no native intrinsic of that name -- otherwise we would shadow
   (and conflict with) the real one. */
#if !MP_HAS_NATIVE_ADDCARRY
  #define _addcarry_u64  mp_addcarry_u64
  #define _subborrow_u64 mp_subborrow_u64
  #define _subborrow_u32 mp_subborrow_u32
#endif

#endif /* MP_CARRY_INTRIN_H */
