#include <assert.h>
#include <gmp.h>

#include "factor/lasieve/asm/siever-config.h"
#include "factor/lasieve/if.h"
#include "factor/lasieve/gmp-aux.h"
#include "factor/lasieve/redu2.h"

int main(void)
{
    char empty[] = "";
    char crlf[] = "123\r\n";
    mpz_t value, q, r;
    i64_t a0, b0, a1, b1;

    mpz_init(value);
    /* An empty field is malformed, not a silent zero: every caller checks the
     * return value and reports the error, so string2mpz must fail here. */
    assert(string2mpz(value, empty, 10) == -1);
    assert(string2mpz(value, crlf, 10) == 0);
    assert(mpz_cmp_ui(value, 123) == 0);

    mpz_init_set_ui(q, 1);
    mpz_mul_2exp(q, q, 128);
    mpz_init_set_ui(r, 0);
    assert(reduce2gmp(&a0, &b0, &a1, &b1, q, r, 1.0) == 1);

    mpz_clear(r);
    mpz_clear(q);
    mpz_clear(value);
    return 0;
}
