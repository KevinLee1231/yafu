#include <assert.h>
#include <gmp.h>

#include "factor/nfs/lasieve/kernels/include/siever-config.h"
#include "factor/nfs/lasieve/include/if.h"
#include "factor/nfs/lasieve/include/gmp-aux.h"
#include "factor/nfs/lasieve/include/redu2.h"

/* 这个驱动链的是单份对象，lasieve_ns 解析成 lasieve_single；per-I 的
 * 名字现在都在命名空间里，所以要显式引进来。 */
using namespace lasieve_ns;

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
