/*--------------------------------------------------------------------
GMP-level entry point for the MPQS module.

yafu code must not include mpqs.h directly.  mpqs.h reaches msieve's mp.h,
which defines mp_t with MAX_MP_WORDS 32, while yafu's core_types.h defines
the same name with 64 -- same name, different size, so the two cannot share a
translation unit.  Everything on the yafu side of the boundary speaks mpz_t
and reaches MPQS only through here.
--------------------------------------------------------------------*/

#ifndef _MPQS_XFACE_H_
#define _MPQS_XFACE_H_

#include <gmp.h>

/* Factor n with the multiple polynomial quadratic sieve.  Returns a
 * malloc'd array of *num_factors mpz_t values, each a divisor of n, or
 * NULL if nothing was found.  Release it with mpqs_xface_free(). */
mpz_t *mpqs_xface(mpz_t n, int *num_factors);

/* Release an array returned by mpqs_xface(). */
void mpqs_xface_free(mpz_t *factors, int num_factors);

#endif /* _MPQS_XFACE_H_ */
