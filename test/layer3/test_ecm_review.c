/* 筛法、队列和线程池边界的回归测试。 */
#include "testkit.h"
#include "soe.h"
#include "soe_impl.h"
#include "threadpool.h"
#include "ytools.h"
#include "yafu_ecm.h"
#include "tinyecm.h"
#ifdef USE_AVX512F
#include "avx_ecm.h"
#endif
#include <gmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_prime_ref(uint64_t n)
{
    uint64_t d;
    if (n < 2)
        return 0;
    if ((n & 1) == 0)
        return n == 2;
    for (d = 3; d <= n / d; d += 2)
        if (n % d == 0)
            return 0;
    return 1;
}

static void t_queue_stack(tk_ctx *tk)
{
    Queue_t *stack = newQueue(2, 1);
    TK_REQUIRE(tk, stack != NULL, "newQueue returned NULL");
    enqueue(stack, 11);
    enqueue(stack, 22);
    enqueue(stack, 33);
    TK_CHECK(tk, stack->len == 2);
    TK_CHECK(tk, peekqueue(stack) == 22);
    TK_CHECK(tk, dequeue(stack) == 22);
    TK_CHECK(tk, dequeue(stack) == 11);
    clearQueue(stack);
    free(stack);
}

static void t_full_line(tk_ctx *tk)
{
    FILE *f = tmpfile();
    char *line = NULL;
    int size = 0;
    char payload[2200];
    memset(payload, 'q', sizeof payload - 1);
    payload[sizeof payload - 1] = '\0';
    TK_REQUIRE(tk, f != NULL, "tmpfile failed");
    TK_REQUIRE(tk, fputs(payload, f) >= 0, "tmpfile write failed");
    rewind(f);
    line = get_full_line(line, &size, f);
    TK_REQUIRE(tk, line != NULL, "final line without newline was lost");
    TK_CHECK(tk, strcmp(line, payload) == 0);
    TK_CHECK(tk, size >= (int)sizeof payload);
    line = get_full_line(line, &size, f);
    TK_CHECK(tk, line == NULL);
    fclose(f);
}

static void t_tiny_sieve_edges(tk_ctx *tk)
{
    uint32_t primes[16];
    static const uint32_t expected[] = {2, 3, 5, 7};
    uint32_t count, i;
    TK_CHECK(tk, tiny_soe(0, primes) == 0);
    TK_CHECK(tk, tiny_soe(2, primes) == 0);
    TK_CHECK(tk, tiny_soe(3, primes) == 1 && primes[0] == 2);
    TK_CHECK(tk, tiny_soe(4, primes) == 2 && primes[1] == 3);
    count = tiny_soe(10, primes);
    TK_CHECK(tk, count == sizeof expected / sizeof expected[0]);
    for (i = 0; i < count; i++)
        TK_CHECK(tk, primes[i] == expected[i]);
}

static void t_exact_sieve_bound(tk_ctx *tk)
{
    uint32_t primes[200];
    uint32_t count = tiny_soe(1010, primes);
    soe_staticdata_t sdata = {0};
    mpz_t offset;
    mpz_init_set_ui(offset, 0);
    mpz_init(sdata.offset);
    TK_REQUIRE(tk, count > 0 && primes[count - 1] == 1009,
               "test prime table does not end at 1009");
    TK_CHECK(tk, check_input(1009ULL * 1009ULL, 0, count, primes,
                             &sdata, offset) == 0);
    TK_CHECK(tk, sdata.pboundi == count);
    TK_CHECK(tk, sdata.pbound == 1009);
    TK_CHECK(tk, check_input(2000000, 0, 0, NULL, &sdata, offset) != 0);
    mpz_clear(sdata.offset);
    mpz_clear(offset);
}

static void check_offsets_case(tk_ctx *tk, int sieve_range, uint64_t blocks)
{
    thread_soedata_t t;
    uint32_t sieve_p[1] = {1009};
    int roots[1] = {1};
    uint32_t rclass[1] = {1};
    uint64_t *pbounds = calloc((size_t)blocks, sizeof(*pbounds));
    uint32_t offsets[1] = {0};
    uint64_t i;

    TK_REQUIRE(tk, pbounds != NULL, "pbounds allocation failed");
    memset(&t, 0, sizeof(t));
    t.sdata.sieve_p = sieve_p;
    t.sdata.root = roots;
    t.sdata.rclass = rclass;
    t.sdata.startprime = 0;
    t.sdata.bucket_start_id = 1;
    t.sdata.blocks = blocks;
    t.sdata.blk_r = 10;
    t.sdata.prodN = 1;
    t.sdata.sieve_range = sieve_range;
    mpz_init_set_ui(t.sdata.offset, 0);
    t.ddata.pbounds = pbounds;
    t.ddata.offsets = offsets;
    t.ddata.ublk_b = 10;
    t.ddata.blk_b_sqrt = 4;

    get_offsets(&t);
    for (i = 0; i < blocks; i++)
        TK_CHECK(tk, pbounds[i] == 0);

    mpz_clear(t.sdata.offset);
    free(pbounds);
}

static void t_offsets_bounds(tk_ctx *tk)
{
    /* 一个较大的素数同时跨过多个短 block，覆盖两种 offset 路径。 */
    check_offsets_case(tk, 1, 1);
    check_offsets_case(tk, 1, 3);
    check_offsets_case(tk, 0, 1);
    check_offsets_case(tk, 0, 3);
}

static void t_prp_compaction(tk_ctx *tk)
{
    const uint64_t low = 3000, high = 3300;
    uint64_t count, i, expected = 0;
    uint64_t *values;
    soe_staticdata_t *sdata = soe_init(-1, 3, 32);
    mpz_t lo, hi;
    mpz_init_set_ui(lo, low);
    mpz_init_set_ui(hi, high);
    sdata->analysis = 1;
    values = sieve_to_depth(sdata, lo, hi, 0, 1, 7, &count, 0, 0);
    for (i = low; i <= high; i++)
        expected += is_prime_ref(i);
    TK_CHECKF(tk, count == expected, "got %llu values, expected %llu",
              (unsigned long long)count, (unsigned long long)expected);
    for (i = 0; i < count; i++)
    {
        uint64_t actual = low + values[i];
        TK_CHECK(tk, actual >= low && actual <= high);
        TK_CHECKF(tk, is_prime_ref(actual), "composite value %llu survived",
                  (unsigned long long)actual);
        if (i > 0)
            TK_CHECK(tk, values[i - 1] < values[i]);
    }
    free(values);
    soe_finalize(sdata);
    mpz_clears(lo, hi, NULL);
}

static void t_small_prime_range(tk_ctx *tk)
{
    static const uint64_t expected[] = {11, 13, 17, 19, 23, 29};
    soe_staticdata_t *sdata = soe_init(-1, 1, 32);
    uint64_t count = 0;
    uint64_t *primes;
    size_t i;

    primes = soe_wrapper(sdata, 10, 30, 1, &count, 0, 0);
    TK_CHECK(tk, primes == NULL);
    TK_CHECK(tk, count == sizeof expected / sizeof expected[0]);

    primes = soe_wrapper(sdata, 10, 30, 0, &count, 0, 0);
    TK_REQUIRE(tk, primes != NULL, "small range returned no storage");
    TK_CHECK(tk, count == sizeof expected / sizeof expected[0]);
    for (i = 0; i < count && i < sizeof expected / sizeof expected[0]; i++)
        TK_CHECK(tk, primes[i] == expected[i]);

    free(primes);

    primes = soe_wrapper(sdata, 14, 16, 1, &count, 0, 0);
    TK_CHECK(tk, primes == NULL);
    TK_CHECK(tk, count == 0);
    primes = soe_wrapper(sdata, 14, 16, 0, &count, 0, 0);
    TK_CHECK(tk, count == 0);
    free(primes);

    soe_finalize(sdata);
}

static void t_large_offset_range(tk_ctx *tk)
{
    const uint64_t width = 1000100;
    soe_staticdata_t *sdata = soe_init(-1, 1, 32);
    unsigned char *composite = calloc((size_t)width + 1, 1);
    uint64_t count = 0, expected = 0, i;
    uint64_t *values;
    uint32_t j;
    mpz_t lo, hi;

    TK_REQUIRE(tk, composite != NULL, "reference sieve allocation failed");
    mpz_inits(lo, hi, NULL);
    mpz_ui_pow_ui(lo, 10, 100);
    mpz_add_ui(hi, lo, (unsigned long)width);
    sdata->analysis = 1;
    /* 独立按每个筛选素数划去倍数，不依赖生产代码的计数路径。 */
    for (j = 0; j < sdata->num_sp; j++)
    {
        uint32_t p = sdata->sieve_p[j];
        uint64_t first = (p - mpz_fdiv_ui(lo, p)) % p;
        for (i = first; i <= width; i += p)
            composite[i] = 1;
    }
    values = sieve_to_depth(sdata, lo, hi, 0, 0, 7, &count, 0, 0);
    TK_REQUIRE(tk, values != NULL, "large-offset sieve returned no storage");
    for (i = 0; i <= width; i++)
    {
        if (!composite[i])
        {
            if (expected < count)
                TK_EQ_U64(tk, values[expected], i);
            expected++;
        }
    }
    TK_EQ_U64(tk, count, expected);
    free(values);
    free(composite);

    mpz_set_ui(lo, 1);
    mpz_set_ui(hi, 1);
    mpz_mul_2exp(hi, hi, 64);
    mpz_add_ui(hi, hi, 2);
    values = sieve_to_depth(sdata, lo, hi, 0, 0, 7, &count, 0, 0);
    TK_CHECK(tk, values == NULL);
    TK_EQ_U64(tk, count, 0);
    free(values);
    soe_finalize(sdata);
    mpz_clears(lo, hi, NULL);
}

static void dummy_work(void *arg)
{
    (void)arg;
}

static void t_threadpool_bounds(tk_ctx *tk)
{
    int i;
    tpool_t *pool = tpool_setup(0, NULL, NULL, NULL, NULL, NULL);
    TK_REQUIRE(tk, pool != NULL, "tpool_setup returned NULL");
    TK_CHECK(tk, pool[0].num_threads == 1);
    for (i = 0; i < 17; i++)
        tpool_add_work_fcn(pool, dummy_work);
    TK_CHECK(tk, pool[0].num_work_fcn == 16);
    free(pool);
}

static void t_ecm_level_bounds(tk_ctx *tk)
{
    ecm_obj_t ecm = {0};

    TK_CHECK(tk, get_curves_for_tlevel(10, 0, 0) == 69471);
    TK_CHECK(tk, get_curves_for_tlevel(11, 0, 0) == 0);

    record_curves_completed(&ecm, 100000, 850000000, 0, 0);
    TK_CHECK(tk, ecm.tlevels[10] >= 1.0);
    TK_CHECK(tk, ecm.total_work == 65.0);

    free(ecm.curve_b1_rec);
    free(ecm.curve_b2_rec);
    free(ecm.param_rec);
    free(ecm.num_rec);
}

static void t_tinyecm_custom_bound(tk_ctx *tk)
{
    uint64_t state = 1;
    mpz_t n, factor;
    mpz_inits(n, factor, NULL);
    mpz_setbit(n, 89);
    mpz_sub_ui(n, n, 1);
    tinyecm(n, factor, 100, 101, 1, &state, 0);
    TK_CHECK(tk, mpz_cmp_ui(factor, 1) == 0);
    mpz_clears(n, factor, NULL);
}

#ifdef USE_AVX512F
static void t_vec_shift52(tk_ctx *tk)
{
    static const int shifts[] = {0, 1, 17, 52};
    static const uint32_t masks[] = {0, 0x55, 0xff};
    uint32_t words, lane, limb, mask_index, shift_index, single;
    mpz_t value, expected;
    mpz_inits(value, expected, NULL);
    for (words = 0; words <= 2; words++)
    for (mask_index = 0; mask_index < 3; mask_index++)
    for (shift_index = 0; shift_index < 4; shift_index++)
    for (single = 0; single <= (shifts[shift_index] == 1); single++)
    {
        ALIGNED_MEM uint64_t data[24];
        uint64_t before[24];
        vec_bignum_t vector = {0};
        uint32_t mask = masks[mask_index], overflow, expected_overflow = 0;
        for (limb = 0; limb < 3; limb++)
        for (lane = 0; lane < 8; lane++)
            data[limb * 8 + lane] = UINT64_C(0xfffffffffffff) - lane - limb;
        memcpy(before, data, sizeof data);
        vector.data = data;
        vector.WORDS_ALLOC = words;
        overflow = single ? vec_bignum52_mask_lshift_1(&vector, mask) :
            vec_bignum52_mask_lshift_n(&vector, shifts[shift_index], mask);
        for (lane = 0; lane < 8; lane++)
        {
            mpz_set_ui(value, 0);
            for (limb = words; limb > 0; limb--)
            {
                mpz_mul_2exp(value, value, 52);
                mpz_add_ui(value, value, before[(limb - 1) * 8 + lane]);
            }
            mpz_mul_2exp(expected, value, shifts[shift_index]);
            for (limb = 0; limb <= words; limb++)
            {
                uint64_t want = mpz_get_ui(expected) & UINT64_C(0xfffffffffffff);
                if ((mask & (1u << lane)) == 0)
                    want = before[limb * 8 + lane];
                else if (limb == words && want != 0)
                    expected_overflow |= 1u << lane;
                TK_EQ_U64(tk, data[limb * 8 + lane], want);
                mpz_fdiv_q_2exp(expected, expected, 52);
            }
        }
        TK_EQ_U64(tk, overflow, expected_overflow);
    }
    mpz_clears(value, expected, NULL);
}
#endif

static const tk_test tests[] = {
    {"queue_stack", t_queue_stack, "fast ecm-queue"},
    {"full_line", t_full_line, "fast ecm-line"},
    {"tiny_sieve_edges", t_tiny_sieve_edges, "fast ecm-tiny-sieve"},
    {"exact_sieve_bound", t_exact_sieve_bound, "fast ecm-sieve-bound"},
    {"offsets_bounds", t_offsets_bounds, "fast ecm-offsets"},
    {"prp_compaction", t_prp_compaction, "fast ecm-prp"},
    {"small_prime_range", t_small_prime_range, "fast ecm-small-range"},
    {"large_offset_range", t_large_offset_range, "fast ecm-large-offset"},
    {"threadpool_bounds", t_threadpool_bounds, "fast ecm-threadpool"},
    {"ecm_level_bounds", t_ecm_level_bounds, "fast ecm-levels"},
    {"tinyecm_custom_bound", t_tinyecm_custom_bound, "fast ecm-custom-bound"},
#ifdef USE_AVX512F
    {"vec_shift52", t_vec_shift52, "fast ecm-shift52"},
#endif
};

const tk_module tk_module_ecm_review = {
    "ecm_review", "sieve, queue, and thread-pool boundary regressions",
    tests, (int)(sizeof tests / sizeof tests[0])
};
