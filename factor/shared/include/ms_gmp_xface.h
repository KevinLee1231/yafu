/*--------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Jason Papadopoulos. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

$Id: gmp_xface.h 1033 2020-09-04 16:43:27Z jasonp_sf $
--------------------------------------------------------------------*/

#ifndef _GMP_XFACE_H_
#define _GMP_XFACE_H_

#include <util.h>
#include <gmp.h>
#include <mp.h>

#ifdef __MINGW32__
#include "mpz-ull.h"
#else
#define mpz_set_ull mpz_set_ui
#define mpz_get_ull mpz_get_ui
#endif

#ifdef __cplusplus
extern "C" {
#endif

	/* Note that when GMP_LIMB_BITS == 64 it is possible
	   to use mpz_set_{ui|si}, except that 64-bit
	   MSVC forces the input argument in these calls to
	   be 32 bits in size and not 64 */

/*--------------------------------------------------------------------*/
static INLINE void mp2gmp(mp_t *src, mpz_t dest) {

	mpz_import(dest, (size_t)(src->nwords), -1, sizeof(uint32), 
			0, (size_t)0, src->val);
}

/*--------------------------------------------------------------------*/
static INLINE void gmp2mp(mpz_t src, mp_t *dest) {

	size_t count;
	size_t bits = mpz_sizeinbase(src, 2);

	if (bits > 32 * MAX_MP_WORDS) {
		/* 固定长度目标无法表示该数，明确停止而不是写出数组。 */
		fprintf(stderr, "gmp2mp input exceeds %u bits\n",
			(unsigned)(32 * MAX_MP_WORDS));
		exit(-1);
	}

	mp_clear(dest);
	mpz_export(dest->val, &count, -1, sizeof(uint32),
			0, (size_t)0, src);
	dest->nwords = (uint32)count;
}

/*--------------------------------------------------------------------*/
static INLINE void uint64_2gmp(uint64 src, mpz_t dest) {

#if GMP_LIMB_BITS == 64
	mpz_set_ull(dest, src);
#else
	/* mpz_import is terribly slow */
	mpz_set_ui(dest, (uint32)(src >> 32));
	mpz_mul_2exp(dest, dest, 32);
	mpz_add_ui(dest, dest, (uint32)src);
#endif
}

/*--------------------------------------------------------------------*/
static INLINE void int64_2gmp(int64 src, mpz_t dest) {

	if (src < 0) {
		/* 先加一再取反，避免 INT64_MIN 上的有符号溢出。 */
		uint64 magnitude = (uint64)(-(src + 1)) + 1;
		uint64_2gmp(magnitude, dest);
		mpz_neg(dest, dest);
	}
	else {
		uint64_2gmp((uint64)src, dest);
	}
}

/*--------------------------------------------------------------------*/
static INLINE uint64 gmp2uint64(mpz_t src) {

	/* mpz_export is terribly slow */
	//uint64 ans = mpz_getlimbn(src, 0);
	uint64 ans = mpz_get_ull(src);

#if GMP_LIMB_BITS == 32
	if (mpz_size(src) >= 2)
		ans |= (uint64)mpz_getlimbn(src, 1) << 32;
#endif
        return ans;
}

/*--------------------------------------------------------------------*/
static INLINE int64 gmp2int64(mpz_t src) {
	uint64 magnitude;
	const uint64 sign_bit = (uint64)1 << 63;
	if (mpz_sizeinbase(src, 2) > 64) {
		fprintf(stderr, "gmp2int64 input is out of range\n");
		exit(-1);
	}

	if (mpz_cmp_ui(src, 0) < 0) {
		// when ULL_NO_UL is active and GMP_BITS_PER_ULONG = 32, then
		// this conversion doesn't work, it is assumed that the 
		// input is non-negative.  But it works in all cases if the input
		// is non-negative.  So we do a quick double negation.
		mpz_neg(src, src);
		magnitude = gmp2uint64(src);
		mpz_neg(src, src);
		if (magnitude == sign_bit)
			return (int64)(-9223372036854775807LL - 1LL);
		if (magnitude > sign_bit) {
			fprintf(stderr, "gmp2int64 input is out of range\n");
			exit(-1);
		}
		return -(int64)magnitude;
	}
	else {
		magnitude = gmp2uint64(src);
		if (magnitude >= sign_bit) {
			fprintf(stderr, "gmp2int64 input is out of range\n");
			exit(-1);
		}
		return (int64)magnitude;
	}
}


#ifdef __cplusplus
}
#endif

#endif /* _GMP_XFACE_H_ */
