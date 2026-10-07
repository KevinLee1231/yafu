/*----------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Ben Buhrow. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

Some parts of the code (and also this header), included in this 
distribution have been reused from other sources. In particular I 
have benefitted greatly from the work of Jason Papadopoulos's msieve @ 
www.boo.net/~jasonp, Scott Contini's mpqs implementation, and Tom St. 
Denis Tom's Fast Math library.  Many thanks to their kind donation of 
code to the public domain.
       				   --bbuhrow@gmail.com 11/24/09
----------------------------------------------------------------------*/

#ifndef ARITH_H
#define ARITH_H

#include <stdint.h>
#include <sys/types.h>
#include "gmp.h"

#define LIMB_BLKSZ 10	
#define MAX_DIGITS 100
#define MP_RADIX 4294967296.0
#define LN2		0.69314718055994530942

// types of numbers
#define PRIME 0
#define PRP 1
#define COMPOSITE 2
#define UNKNOWN 3


/* Leading / trailing zero count now go through <bit>.
 *
 * This used to be three compiler-specific branches (ICC / GCC / MSVC) spelling
 * out the same operations -- and they did not agree.  The GCC branch used
 * __builtin_ctz* / __builtin_clz*, which are undefined for an argument of 0;
 * the ICC and MSVC branches used _BitScan* with an explicit zero test returning
 * 32 or 64.  The same code therefore read an undefined value on Linux and 32/64
 * on Windows.  <bit> settles both: std::countr_zero(0) and std::countl_zero(0)
 * return the bit width, by definition.  GCC lowers them to the same tzcnt /
 * lzcnt the BMI paths emitted, so nothing gets slower -- one implementation and
 * one behaviour everywhere replace three.
 *
 * Clearing the lowest set bit stays as "x &= x - 1" for now: the C++26 spelling
 * would be std::reset_lowest_bit (P2983), but libstdc++ 16 does not ship it yet.
 * Both spellings compile to blsr / lea+and; swap it in when the library catches up.
 */
#include <bit>

/* <bit> 的这几个函数只接受无符号整数类型，而 __builtin_ctz* / __builtin_clz*
 * 靠隐式转换就接受了——仓库里确实有传 int64_t 的调用点（squfof.cpp）。所以
 * 宏里补上转换，语义就是按二进制补码数位，与原来一致。 */
#define _reset_lsb(x)    ((x) &= ((x) - 1))
#define _reset_lsb64(x)  ((x) &= ((x) - 1))
#define _lead_zcnt64(x)  (std::countl_zero(static_cast<uint64_t>(x)))
#define _trail_zcnt(x)   (std::countr_zero(static_cast<uint32_t>(x)))
#define _trail_zcnt64(x) (std::countr_zero(static_cast<uint64_t>(x)))

#if defined(__SIZEOF_INT128__) && (__SIZEOF_INT128__ == 16)
#define HAS_UINT128
typedef __uint128_t uint128_t;
#endif

/********************* single precision arith **********************/
void spAdd(uint64_t u, uint64_t v, uint64_t *sum, uint64_t *carry);
void spAdd3(uint64_t u, uint64_t v, uint64_t w, uint64_t *sum, uint64_t *carry);
void spSub3(uint64_t u, uint64_t v, uint64_t w, uint64_t *sub, uint64_t *borrow);
void spSub(uint64_t u, uint64_t v, uint64_t *sub, uint64_t *borrow);
void spMultiply(uint64_t u, uint64_t v, uint64_t *product, uint64_t *carry);
void spMulAdd(uint64_t u, uint64_t v, uint64_t w, uint64_t t, uint64_t *lower, uint64_t *carry);
void spMulMod(uint64_t u, uint64_t v, uint64_t m, uint64_t *w);
void spModExp(uint64_t a, uint64_t b, uint64_t m, uint64_t *u);
uint64_t spDivide(uint64_t *q, uint64_t *r, uint64_t u[2], uint64_t v);
uint64_t spBits(uint64_t n);
int bits64(uint64_t n);
uint32_t modinv_1(uint32_t a, uint32_t p);
uint32_t modinv_1b(uint32_t a, uint32_t p);
uint32_t modinv_1c(uint32_t a, uint32_t p);
uint64_t spPRP2(uint64_t p);
void ShanksTonelli_1(uint64_t a, uint64_t p, uint64_t *sq);
uint64_t spGCD(uint64_t x, uint64_t y);
uint64_t spBinGCD(uint64_t x, uint64_t y);
uint64_t spBinGCD_odd(uint64_t u, uint64_t v);
uint64_t gcd64(uint64_t x, uint64_t y);
uint64_t bingcd64(uint64_t x, uint64_t y);
void dblGCD(double x, double y, double* w);
int jacobi_1(uint64_t n, uint64_t p);
int ndigits_1(uint64_t n);
int uint128_div(const uint64_t dividend[2], const uint64_t divisor[2],
    uint64_t quotient[2], uint64_t remainder[2]);
uint64_t u64div(uint64_t c, uint64_t n);


/********************* a few gmp-based utilities **********************/
double zlog(mpz_t x);
int llt(uint32_t exp, int vflag);
int gmp_base10(mpz_t x);
int is_mpz_prp(mpz_t n, int num_witnesses);
uint64_t mpz_get_64(mpz_t src);
void mpz_set_64(mpz_t dest, uint64_t src);
void build_RSA(int bits, mpz_t n, gmp_randstate_t gmp_randstate);
void gordon(int bits, mpz_t n, gmp_randstate_t gmp_randstate);
void fftmul(mpz_t c, mpz_t a, mpz_t b, int bits_per_word, int fftlen);
#endif
