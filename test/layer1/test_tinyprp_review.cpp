/* tinyprp 标量边界及 128 位模幂与 GMP 参考实现的对照。 */
#include "testkit.h"
#include "tinyprp.h"
#include "mpz_aprcl.h"
#include <gmp.h>
#include <stdint.h>
#include <string.h>

static void mpz_to_u128(uint64_t out[2], const mpz_t value)
{
    mpz_t t;
    mpz_init_set(t, value);
    out[0] = (uint64_t)mpz_get_ui(t);
    mpz_tdiv_q_2exp(t, t, 64);
    out[1] = (uint64_t)mpz_get_ui(t);
    mpz_clear(t);
}

static void u128_to_mpz(mpz_t out, const uint64_t value[2])
{
    mpz_set_ui(out, value[1]);
    mpz_mul_2exp(out, out, 64);
    mpz_add_ui(out, out, value[0]);
}

static int reference_fermat2(const mpz_t n)
{
    mpz_t exponent, result;
    int answer;

    if (mpz_cmp_ui(n, 2) < 0) return 0;
    if (mpz_cmp_ui(n, 2) == 0) return 1;
    if (mpz_even_p(n)) return 0;

    mpz_inits(exponent, result, NULL);
    mpz_sub_ui(exponent, n, 1);
    mpz_set_ui(result, 2);
    mpz_powm(result, result, exponent, n);
    answer = mpz_cmp_ui(result, 1) == 0;
    mpz_clears(exponent, result, NULL);
    return answer;
}

static int reference_mr2(const mpz_t n)
{
    mpz_t d, nminus1, x;
    mp_bitcnt_t s, r;
    int answer = 0;

    if (mpz_cmp_ui(n, 2) < 0) return 0;
    if (mpz_cmp_ui(n, 2) == 0) return 1;
    if (mpz_even_p(n)) return 0;

    mpz_inits(d, nminus1, x, NULL);
    mpz_sub_ui(nminus1, n, 1);
    mpz_set(d, nminus1);
    s = mpz_scan1(d, 0);
    mpz_tdiv_q_2exp(d, d, s);
    mpz_set_ui(x, 2);
    mpz_powm(x, x, d, n);
    if (mpz_cmp_ui(x, 1) == 0 || mpz_cmp(x, nminus1) == 0) {
        answer = 1;
    } else {
        for (r = 1; r < s; r++) {
            mpz_mul(x, x, x);
            mpz_mod(x, x, n);
            if (mpz_cmp(x, nminus1) == 0) {
                answer = 1;
                break;
            }
            if (mpz_cmp_ui(x, 1) == 0) break;
        }
    }
    mpz_clears(d, nminus1, x, NULL);
    return answer;
}

static int reference_modexp_sign(const uint64_t nwords[2],
    const uint64_t ewords[2], uint64_t base)
{
    mpz_t n, e, b, result, nminus1;
    int answer;

    mpz_inits(n, e, b, result, nminus1, NULL);
    u128_to_mpz(n, nwords);
    u128_to_mpz(e, ewords);
    mpz_set_ui(b, base);
    mpz_powm(result, b, e, n);
    mpz_sub_ui(nminus1, n, 1);
    answer = mpz_cmp_ui(result, 1) == 0 ? 1 :
        (mpz_cmp(result, nminus1) == 0 ? -1 : 0);
    mpz_clears(n, e, b, result, nminus1, NULL);
    return answer;
}

static void check_prp_value(tk_ctx *tk, mpz_t n)
{
    uint64_t words[2];
    int selfridge, bpsw;

    mpz_to_u128(words, n);
	selfridge = mpz_selfridge_prp(n);
	bpsw = mpz_bpsw_prp(n);

    TK_EQ_U64(tk, fermat_prp_128x1(words), reference_fermat2(n));
    TK_EQ_U64(tk, MR_2sprp_128x1(words), reference_mr2(n));
	TK_EQ_U64(tk, selfridge_prp_128x1(words), selfridge > PRP_COMPOSITE);
	TK_EQ_U64(tk, bpsw_prp_128x1(words), bpsw > PRP_COMPOSITE);
}

static void t_scalar_boundaries(tk_ctx *tk)
{
    static const char *const wide_values[] = {
        "18446744073709551617",
        "18446744073709551629",
        "79228162514264337593543950319",
        "79228162514264337593543950335",
        "170141183460469231731687303715884105727",
        "340282366920938463463374607431768211455"
    };
    mpz_t n;
    unsigned long value;
    size_t i;

    mpz_init(n);
    for (value = 0; value <= 31; value++) {
        mpz_set_ui(n, value);
        check_prp_value(tk, n);
    }
    for (i = 0; i < sizeof wide_values / sizeof wide_values[0]; i++) {
        TK_REQUIRE(tk, mpz_set_str(n, wide_values[i], 10) == 0,
            "invalid 128-bit test input: %s", wide_values[i]);
        check_prp_value(tk, n);
    }
    mpz_clear(n);
}

static void t_modexp_boundaries(tk_ctx *tk)
{
    static const char *const moduli[] = {
        "18446744073709551629",
        "79228162514264337593543950319",
        "170141183460469231731687303715884105727",
        "340282366920938463463374607431768211455"
    };
    static const uint64_t exponents[][2] = {
        {0, 0}, {1, 0}, {0, 1}, {UINT64_MAX, UINT64_MAX}
    };
    static const uint64_t bases[] = {0, 1, 2, UINT64_MAX};
    mpz_t n;
    uint64_t nwords[2];
    size_t i, j, k;

    mpz_init(n);
    for (i = 0; i < sizeof moduli / sizeof moduli[0]; i++) {
        TK_REQUIRE(tk, mpz_set_str(n, moduli[i], 10) == 0,
            "invalid modulus: %s", moduli[i]);
        mpz_to_u128(nwords, n);
        for (j = 0; j < sizeof exponents / sizeof exponents[0]; j++) {
            for (k = 0; k < sizeof bases / sizeof bases[0]; k++) {
                int expected = reference_modexp_sign(nwords, exponents[j], bases[k]);
                int actual = modexp_128x1b(nwords, (uint64_t *)exponents[j], bases[k]);
                TK_CHECKF(tk, actual == expected,
                    "modexp mismatch for modulus %s, exponent case %u, base case %u",
                    moduli[i], (unsigned)j, (unsigned)k);
            }
        }
    }
	mpz_clear(n);
}

#ifdef USE_AVX512F
static void t_mr52_mixed_lanes(tk_ctx *tk)
{
	uint64_t values[8] = {0, 1, 2, 4, 341, 561, 1105, 2147483647};
	uint8_t mask = MR_2sprp_52x8(values);
	mpz_t n;
	int i;

	mpz_init(n);
	for (i = 0; i < 8; i++) {
		mpz_set_ui(n, values[i]);
		TK_EQ_U64(tk, (mask >> i) & 1U, reference_mr2(n));
	}
	TK_EQ_U64(tk, fermat_prp_64x1(341), 1);
	TK_EQ_U64(tk, (mask >> 4) & 1U, 0);
	mpz_clear(n);
}

static void set_u104_lane(uint64_t packed[16], int lane, const mpz_t value)
{
	uint64_t words[2];
	mpz_to_u128(words, value);
	packed[lane] = words[0] & UINT64_C(0x000fffffffffffff);
	packed[8 + lane] = (words[0] >> 52) | (words[1] << 12);
}

static void t_prp104_mixed_lanes(tk_ctx *tk)
{
	static const char *const values[] = {
		"18014398509481983",
		"18014398509481985",
		"18446744073709551617",
		"18446744073709551629",
		"79228162514264337593543950319",
		"79228162514264337593543950335",
		"10141204801825835211973625643007",
		"20282409603651670423947251286015"
	};
	long p[8] = {1, 1, 3, 1, 5, 1, 7, 1};
	long q[8] = {-1, 2, -1, -3, 2, -5, 3, -7};
	uint64_t packed[16];
	uint8_t mr_mask, lucas_mask, selfridge_mask, bpsw_mask;
	mpz_t n[8];
	int i;

	memset(packed, 0, sizeof(packed));
	for (i = 0; i < 8; i++) {
		mpz_init(n[i]);
		TK_REQUIRE(tk, mpz_set_str(n[i], values[i], 10) == 0,
			"invalid 104-bit input: %s", values[i]);
		set_u104_lane(packed, i, n[i]);
	}

	mr_mask = MR_2sprp_104x8(packed);
	lucas_mask = lucas_104x8(packed, p, q);
	selfridge_mask = selfridge_104x8(packed);
	bpsw_mask = bpsw_104x8(packed);
	for (i = 0; i < 8; i++) {
		TK_EQ_U64(tk, (mr_mask >> i) & 1U, reference_mr2(n[i]));
		TK_CHECKF(tk, ((lucas_mask >> i) & 1U) ==
			(unsigned)(mpz_lucas_prp(n[i], p[i], q[i]) > PRP_COMPOSITE),
			"Lucas lane %d mismatch for %s, p=%ld, q=%ld", i, values[i], p[i], q[i]);
		TK_EQ_U64(tk, (selfridge_mask >> i) & 1U,
			mpz_selfridge_prp(n[i]) > PRP_COMPOSITE);
		TK_EQ_U64(tk, (bpsw_mask >> i) & 1U,
			mpz_bpsw_prp(n[i]) > PRP_COMPOSITE);
		mpz_clear(n[i]);
	}
}
#endif


/* ---------- 随机对拍：与 GMP 参考实现逐个比对 ---------- */

static void random_odd_u128(tk_rng *rng, uint64_t out[2], int bits)
{
    uint64_t hi, lo;

    if (bits <= 64) {
        lo = tk_rng_nbits(rng, bits);
        hi = 0;
    } else {
        hi = tk_rng_nbits(rng, bits - 64);
        lo = tk_rng_u64(rng);
    }
    lo |= 1;                       /* 素性测试只对奇数有意义 */
    if ((hi == 0) && (lo == 1)) lo = 3;
    out[0] = lo;
    out[1] = hi;
}

static void t_random_64bit_mr(tk_ctx *tk)
{
    tk_rng *rng = tk_rng_of(tk);
    mpz_t n;
    int i;

    mpz_init(n);
    for (i = 0; i < 2000; i++) {
        uint64_t value = tk_rng_nbits(rng, 64) | 1ULL;
        if (value < 3) value = 3;
        mpz_set_ui(n, value);
        TK_CHECKF(tk, MR_2sprp_64x1(value) == reference_mr2(n),
            "64-bit MR mismatch at %llu", (unsigned long long)value);
    }
    mpz_clear(n);
}

static void t_random_128bit_prp(tk_ctx *tk)
{
    tk_rng *rng = tk_rng_of(tk);
    mpz_t n;
    uint64_t words[2];
    int i;

    mpz_init(n);
    for (i = 0; i < 1000; i++) {
        random_odd_u128(rng, words, 128);
        u128_to_mpz(n, words);
        TK_CHECKF(tk, fermat_prp_128x1(words) == reference_fermat2(n),
            "128-bit Fermat mismatch at iteration %d", i);
        TK_CHECKF(tk, MR_2sprp_128x1(words) == reference_mr2(n),
            "128-bit MR mismatch at iteration %d", i);
        TK_CHECKF(tk, (selfridge_prp_128x1(words) > 0) ==
            (mpz_selfridge_prp(n) > PRP_COMPOSITE),
            "128-bit Selfridge mismatch at iteration %d", i);
        TK_CHECKF(tk, (bpsw_prp_128x1(words) > 0) ==
            (mpz_bpsw_prp(n) > PRP_COMPOSITE),
            "128-bit BPSW mismatch at iteration %d", i);
    }
    mpz_clear(n);
}

static void t_random_128bit_lucas(tk_ctx *tk)
{
    tk_rng *rng = tk_rng_of(tk);
    mpz_t n;
    uint64_t words[2];
    int i;

    mpz_init(n);
    for (i = 0; i < 1000; i++) {
        long k = (long)tk_rng_range(rng, 64);
        /* Selfridge 的 d 序列：5, -7, 9, -11, 13, ... */
        long d = (k % 2 == 0) ? (4 * k + 5) : -(4 * k + 7);
        long q = (1 - d) / 4;
        int expected;

        random_odd_u128(rng, words, 128);
        u128_to_mpz(n, words);
        expected = (mpz_lucas_prp(n, 1, q) == PRP_PRP);
        TK_CHECKF(tk, (lucas_prp_128x1(words, q) > 0) == expected,
            "128-bit Lucas mismatch at iteration %d, d = %ld", i, d);
    }
    mpz_clear(n);
}

#ifdef USE_AVX512F
static void t_random_104bit_avx512(tk_ctx *tk)
{
    tk_rng *rng = tk_rng_of(tk);
    uint64_t packed[16];
    uint64_t words[8][2];
    mpz_t values[8];
    int i, j;

    for (j = 0; j < 8; j++) mpz_init(values[j]);
    for (i = 0; i < 400; i++) {
        uint8_t mask;

        memset(packed, 0, sizeof(packed));
        for (j = 0; j < 8; j++) {
            int bits = 54 + (int)tk_rng_range(rng, 51);   /* 54..104 位 */
            random_odd_u128(rng, words[j], bits);
            u128_to_mpz(values[j], words[j]);
            set_u104_lane(packed, j, values[j]);
        }
        mask = MR_2sprp_104x8(packed);
        for (j = 0; j < 8; j++) {
            TK_CHECKF(tk, ((mask >> j) & 1U) == (unsigned)MR_2sprp_128x1(words[j]),
                "AVX512 104-bit MR lane %d mismatch at iteration %d", j, i);
        }
    }
    for (j = 0; j < 8; j++) mpz_clear(values[j]);
}
#endif

static const tk_test tests[] = {
	{"scalar_boundaries", t_scalar_boundaries, "fast tinyprp-review"},
	{"modexp_boundaries", t_modexp_boundaries, "fast tinyprp-review"},
	{"random_64bit_mr", t_random_64bit_mr, "fast tinyprp-review"},
	{"random_128bit_prp", t_random_128bit_prp, "fast tinyprp-review"},
	{"random_128bit_lucas", t_random_128bit_lucas, "fast tinyprp-review"},
#ifdef USE_AVX512F
	{"random_104bit_avx512", t_random_104bit_avx512, "fast tinyprp-review"},
	{"mr52_mixed_lanes", t_mr52_mixed_lanes, "fast tinyprp-review"},
	{"prp104_mixed_lanes", t_prp104_mixed_lanes, "fast tinyprp-review"}
#endif
};

const tk_module tk_module_tinyprp_review = {
    "tinyprp_review", "tinyprp scalar boundaries and GMP comparisons", tests,
    (int)(sizeof tests / sizeof tests[0])
};
