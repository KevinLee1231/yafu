/* 参数极值、独立 Lucas 实现及 APR-CL 并发调用的回归。 */
#include "testkit.h"
#include "mpz_aprcl.h"
#include "jacobi_sum.h"
#include <limits.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

extern int mpz_lucas_prp_monty(mpz_t n, long p, long q);

static void t_jacobi_table_bounds(tk_ctx *tk)
{
    int i;
    for (i = 0; i < JPQSMAX; i++) {
        int p = jpqs[i].p == 1 || jpqs[i].p == 4 ? 2 : jpqs[i].p;
        int q = jpqs[i].q - 1, power = 1, length;
        TK_REQUIRE(tk, p >= 2 && q > 0, "invalid Jacobi table key");
        while (q % p == 0) { power *= p; q /= p; }
        length = power - power / p;
        TK_CHECK(tk, jpqs[i].index >= 0 && jpqs[i].index <= SLSMAX + 1 - length);
        if (i + 1 < JPQSMAX)
            TK_EQ_U64(tk, jpqs[i].index + length, jpqs[i + 1].index);
    }
    for (i = 0; i <= SLSMAX; i++)
        TK_CHECK(tk, sls[i] >= SLSMINVAL && sls[i] <= SLSMAXVAL);
}

static void t_lucas_variants(tk_ctx *tk)
{
    mpz_t n;
    unsigned long value;
    long p, q;
    mpz_init(n);
    for (value = 3; value < 100; value += 2) {
        mpz_set_ui(n, value);
        for (p = -5; p <= 5; p++) for (q = -5; q <= 5; q++) {
            TK_EQ_U64(tk, mpz_lucas_prp_monty(n, p, q), mpz_lucas_prp(n, p, q));
        }
    }
    mpz_set_ui(n, 101);
    TK_EQ_U64(tk, mpz_lucas_prp_monty(n, LONG_MIN, LONG_MAX),
                  mpz_lucas_prp(n, LONG_MIN, LONG_MAX));
    TK_CHECK(tk, mpz_stronglucas_prp(n, LONG_MAX, LONG_MIN) >= PRP_COMPOSITE);
    TK_CHECK(tk, mpz_fibonacci_prp(n, LONG_MAX, -1) >= PRP_COMPOSITE);
    TK_CHECK(tk, mpz_extrastronglucas_prp(n, LONG_MAX) >= PRP_COMPOSITE);
    mpz_set_ui(n, 21);
    TK_EQ_U64(tk, mpz_extrastronglucas_prp(n, 5), PRP_COMPOSITE);
    mpz_clear(n);
}

#if !defined(_WIN32)
typedef struct { unsigned int index; unsigned int failures; } aprcl_work;
static void* check_primes(void* opaque)
{
    aprcl_work* work = opaque;
    unsigned long i;
    mpz_t n;
    mpz_init(n);
    for (i = 101 + 2 * work->index; i < 1000; i += 8) {
        int expected, actual;
        mpz_set_ui(n, i);
        expected = mpz_probab_prime_p(n, 25) != 0;
        actual = mpz_aprtcle(n, APRTCLE_VERBOSE0);
        if (actual != (expected ? APRTCLE_PRIME : APRTCLE_COMPOSITE)) work->failures++;
    }
    /* 多精度输入实际进入 Jacobi 和工作数组计算。 */
    for (i = 0; i < 4; i++) {
        mpz_set_ui(n, 1);
        mpz_mul_2exp(n, n, i % 2 ? 127 : 89);
        mpz_sub_ui(n, n, 1);
        if (mpz_aprtcle(n, APRTCLE_VERBOSE0) != APRTCLE_PRIME) work->failures++;
        mpz_mul(n, n, n);
        if (mpz_aprtcle(n, APRTCLE_VERBOSE0) != APRTCLE_COMPOSITE) work->failures++;
    }
    mpz_clear(n);
    return NULL;
}
static void t_parallel_primality(tk_ctx *tk)
{
    pthread_t threads[4];
    aprcl_work work[4] = {{0,0}, {1,0}, {2,0}, {3,0}};
    unsigned int i, started = 0;
    for (i = 0; i < 4; i++) {
        int status = pthread_create(&threads[i], NULL, check_primes, &work[i]);
        TK_CHECK(tk, status == 0);
        if (status != 0) break;
        started++;
    }
    for (i = 0; i < started; i++) {
        TK_CHECK(tk, pthread_join(threads[i], NULL) == 0);
        TK_EQ_U64(tk, work[i].failures, 0);
    }
}
#endif

static const tk_test tests[] = {
    {"jacobi_table_bounds", t_jacobi_table_bounds, "fast aprcl-review"},
    {"lucas_variants", t_lucas_variants, "fast aprcl-review"},
#if !defined(_WIN32)
    {"parallel_primality", t_parallel_primality, "fast aprcl-review"},
#endif
};
const tk_module tk_module_aprcl_review = {
    "aprcl_review", "Lucas parameters and APR-CL thread isolation", tests,
    (int)(sizeof tests / sizeof tests[0])
};
