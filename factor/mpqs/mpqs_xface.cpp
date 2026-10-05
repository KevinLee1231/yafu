#include <stdlib.h>
#include "mpqs.h"
#include "mpqs_xface.h"

mpz_t *mpqs_xface(mpz_t n, int *num_factors)
{
	msieve_obj *obj;
	mp_t mn;
	factor_list_t flist;
	mpz_t *out;
	uint32 i, nout = 0;

	*num_factors = 0;

	gmp2mp(n, &mn);
	factor_list_init(&flist);

	/* No flags means no log file and no save file.  factor_mpqs only
	 * wants the object for logprintf and for mp_sprintf scratch space. */
	obj = msieve_obj_new(NULL, 0, NULL, NULL, NULL,
				0, 0, 0, cpu_generic, 0, 0, 1, 0, NULL);

	factor_mpqs(obj, &mn, &flist);

	out = (mpz_t *)malloc(sizeof(mpz_t) * (flist.num_factors + 1));
	if (out == NULL)
	{
		factor_list_free(&mn, &flist, obj);
		msieve_obj_free(obj);
		return NULL;
	}

	/* A composite entry is a divisor the sieve could only split partway.
	 * It is still a factor of n, so report it as-is and let the caller
	 * decide whether to recurse. */
	for (i = 0; i < flist.num_factors; i++)
	{
		final_factor_t *ff = flist.final_factors[i];

		if (ff == NULL)
			continue;

		mpz_init(out[nout]);
		mp2gmp(&ff->factor, out[nout]);
		nout++;
	}

	*num_factors = (int)nout;

	factor_list_free(&mn, &flist, obj);
	msieve_obj_free(obj);

	if (nout == 0)
	{
		free(out);
		return NULL;
	}

	return out;
}

void mpqs_xface_free(mpz_t *factors, int num_factors)
{
	int i;

	if (factors == NULL)
		return;

	for (i = 0; i < num_factors; i++)
		mpz_clear(factors[i]);

	free(factors);
}
