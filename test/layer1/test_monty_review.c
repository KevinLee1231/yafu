/* 用 GMP 和普通整数运算检查内联算术的边界。 */
#include "testkit.h"
#include "monty.h"
#include "gmp_u64_xface.h"
#include <gmp.h>

static void t_signed_gmp(tk_ctx *tk)
{
    const int64_t values[] = {INT64_MIN, INT64_MIN + 1, -1, 0, 1, INT64_MAX};
    size_t i;
    mpz_t value;
    mpz_init(value);
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int64_2gmp(values[i], value);
        TK_CHECK(tk, gmp2int64(value) == values[i]);
        TK_CHECK(tk, mpz_sgn(value) == ((values[i] > 0) - (values[i] < 0)));
    }
    mpz_clear(value);
}

static void t_gcd64_edges(tk_ctx *tk)
{
    tk_rng *rng = tk_rng_of(tk);
    unsigned int i;
    TK_EQ_U64(tk, bin_gcd64(0, 0), 0);
    TK_EQ_U64(tk, bin_gcd64(12, 18), 6);
    for (i = 0; i < 2000; i++) {
        uint64_t a = tk_rng_u64(rng), b = tk_rng_u64(rng);
        uint64_t x = a, y = b;
        while (y) { uint64_t r = x % y; x = y; y = r; }
        TK_EQ_U64(tk, bin_gcd64(a, b), x);
    }
}

#ifdef USE_AVX512F
static void t_borrow52(tk_ctx *tk)
{
    uint64_t input[8] = {0, 1, 0, 7, 0, 0, 9, 0};
    uint64_t result[8];
    unsigned int mask, lane;
    for (mask = 0; mask < 256; mask++) {
        __mmask8 carry;
        __m512i value = _mm512_subborrow_epi52(_mm512_loadu_si512(input),
                                              (__mmask8)mask, &carry);
        _mm512_storeu_si512(result, value);
        for (lane = 0; lane < 8; lane++) {
            uint64_t sub = (mask >> lane) & 1;
            TK_EQ_U64(tk, result[lane], (input[lane] - sub) & UINT64_C(0xfffffffffffff));
            TK_CHECK(tk, ((carry >> lane) & 1) == (sub && input[lane] == 0));
        }
    }
}

#if defined(__SIZEOF_INT128__) && __SIZEOF_INT128__ == 16
static void t_gcd128_edges(tk_ctx *tk)
{
    unsigned int bit;
    for (bit = 0; bit < 128; bit++) {
        uint128_t value = (uint128_t)1 << bit;
        uint64_t a[2] = { (uint64_t)value, (uint64_t)(value >> 64) };
        uint64_t b[2] = { a[0], a[1] }, result[2];
        bin_gcd128(a, b, result);
        TK_EQ_U64(tk, result[0], (uint64_t)value);
        TK_EQ_U64(tk, result[1], (uint64_t)(value >> 64));
    }
    for (bit = 0; bit < 1000; bit++) {
        uint64_t a[2] = {tk_rng_u64(tk_rng_of(tk)), tk_rng_u64(tk_rng_of(tk))};
        uint64_t b[2] = {tk_rng_u64(tk_rng_of(tk)), tk_rng_u64(tk_rng_of(tk))};
        uint64_t result[2], expected[2];
        gcd128(a, b, expected);
        bin_gcd128(a, b, result);
        TK_EQ_U64(tk, result[0], expected[0]);
        TK_EQ_U64(tk, result[1], expected[1]);
    }
}

static void t_mul52(tk_ctx *tk)
{
    unsigned int i, lane;
    for (i = 0; i < 256; i++) {
        uint64_t a[8], b[8], lo[8], hi[8];
        __m512i low, high;
        for (lane = 0; lane < 8; lane++) {
            a[lane] = tk_rng_u64(tk_rng_of(tk)) & UINT64_C(0xfffffffffffff);
            b[lane] = tk_rng_u64(tk_rng_of(tk)) & UINT64_C(0xfffffffffffff);
        }
        mul52lohi(_mm512_loadu_si512(a), _mm512_loadu_si512(b), &low, &high);
        _mm512_storeu_si512(lo, low);
        _mm512_storeu_si512(hi, high);
        for (lane = 0; lane < 8; lane++) {
            uint128_t product = (uint128_t)a[lane] * b[lane];
            TK_EQ_U64(tk, lo[lane], (uint64_t)product & UINT64_C(0xfffffffffffff));
            TK_EQ_U64(tk, hi[lane], (uint64_t)(product >> 52));
        }
    }
}
#endif

static void t_redc104_exact(tk_ctx *tk)
{
    mpz_t n, a, radix, inverse, expected, temp;
    unsigned int i, lane;
    mpz_inits(n, a, radix, inverse, expected, temp, NULL);
    mpz_setbit(radix, 104);
    for (i = 0; i < 256; i++) {
        uint64_t n0[8], n1[8], a0[8], a1[8], rho[8], out0[8], out1[8];
        __m512i c0, c1;
        for (lane = 0; lane < 8; lane++) {
            n0[lane] = (tk_rng_u64(tk_rng_of(tk)) & UINT64_C(0xfffffffffffff)) | 1;
            n1[lane] = 1 + tk_rng_u64(tk_rng_of(tk)) % 32;
            a0[lane] = tk_rng_u64(tk_rng_of(tk)) & UINT64_C(0xfffffffffffff);
            a1[lane] = tk_rng_u64(tk_rng_of(tk)) % (n1[lane] + 1);
            if (a1[lane] == n1[lane]) a0[lane] %= n0[lane];
            rho[lane] = 0 - multiplicative_inverse(n0[lane]);
        }
        mask_sqrredc104_exact_vec(&c1, &c0, 0xff,
            _mm512_loadu_si512(a1), _mm512_loadu_si512(a0),
            _mm512_loadu_si512(n1), _mm512_loadu_si512(n0), _mm512_loadu_si512(rho));
        _mm512_storeu_si512(out0, c0);
        _mm512_storeu_si512(out1, c1);
        for (lane = 0; lane < 8; lane++) {
            mpz_set_ui(n, n1[lane]); mpz_mul_2exp(n, n, 52); mpz_add_ui(n, n, n0[lane]);
            mpz_set_ui(a, a1[lane]); mpz_mul_2exp(a, a, 52); mpz_add_ui(a, a, a0[lane]);
            mpz_invert(inverse, radix, n);
            mpz_mul(expected, a, a); mpz_mul(expected, expected, inverse); mpz_mod(expected, expected, n);
            mpz_set_ui(temp, out1[lane]); mpz_mul_2exp(temp, temp, 52); mpz_add_ui(temp, temp, out0[lane]);
            TK_CHECK(tk, mpz_cmp(expected, temp) == 0);
        }
    }
    mpz_clears(n, a, radix, inverse, expected, temp, NULL);
}
#endif

static const tk_test tests[] = {
    {"signed_gmp", t_signed_gmp, "fast monty-review"},
    {"gcd64_edges", t_gcd64_edges, "fast monty-review"},
#ifdef USE_AVX512F
    {"borrow52", t_borrow52, "fast monty-review"},
#if defined(__SIZEOF_INT128__) && __SIZEOF_INT128__ == 16
    {"gcd128_edges", t_gcd128_edges, "fast monty-review"},
    {"mul52", t_mul52, "fast monty-review"},
#endif
    {"redc104_exact", t_redc104_exact, "fast monty-review"},
#endif
};
const tk_module tk_module_monty_review = {
    "monty_review", "inline arithmetic boundary regressions", tests,
    (int)(sizeof tests / sizeof tests[0])
};
