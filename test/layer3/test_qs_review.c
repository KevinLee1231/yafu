/*----------------------------------------------------------------------
 Layer 3 -- targeted regressions for shared QS support code.
----------------------------------------------------------------------*/
#include "testkit.h"
#include "thread.h"
#include "smallmpqs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    volatile int worker_init;
    volatile int worker_shutdown;
    volatile int task_init;
    volatile int task_run;
    volatile int task_shutdown;
} thread_counts_t;

static void count_worker_init(void *data, int thread_num)
{
    thread_counts_t *counts = (thread_counts_t *)data;
    (void)thread_num;
    __sync_fetch_and_add(&counts->worker_init, 1);
}

static void count_worker_shutdown(void *data, int thread_num)
{
    thread_counts_t *counts = (thread_counts_t *)data;
    (void)thread_num;
    __sync_fetch_and_add(&counts->worker_shutdown, 1);
}

static void count_task_init(void *data, int thread_num)
{
    thread_counts_t *counts = (thread_counts_t *)data;
    (void)thread_num;
    __sync_fetch_and_add(&counts->task_init, 1);
}

static void count_task_run(void *data, int thread_num)
{
    thread_counts_t *counts = (thread_counts_t *)data;
    (void)thread_num;
    __sync_fetch_and_add(&counts->task_run, 1);
}

static void count_task_shutdown(void *data, int thread_num)
{
    thread_counts_t *counts = (thread_counts_t *)data;
    (void)thread_num;
    __sync_fetch_and_add(&counts->task_shutdown, 1);
}

static void t_threadpool_lifecycle(tk_ctx *tk)
{
    thread_counts_t counts = {0};
    thread_control_t worker = {
        count_worker_init, count_worker_shutdown, &counts
    };
    task_control_t task = {
        count_task_init, count_task_run, count_task_shutdown, &counts
    };
    struct threadpool *pool;
    int i;

    TK_CHECK(tk, threadpool_init(0, 1, &worker) == NULL);
    TK_CHECK(tk, threadpool_init(1, 0, &worker) == NULL);
    TK_CHECK(tk, threadpool_init(1, 1, NULL) == NULL);
    threadpool_free(NULL);

    pool = threadpool_init(2, 8, &worker);
    TK_REQUIRE(tk, pool != NULL, "threadpool_init failed");
    for (i = 0; i < 32; i++)
        TK_REQUIRE(tk, threadpool_add_task(pool, &task, 1) == 0,
                   "threadpool_add_task failed at %d", i);
    TK_CHECK(tk, threadpool_drain(pool, 1) == 0);
    TK_EQ_U64(tk, counts.task_init, 32);
    TK_EQ_U64(tk, counts.task_run, 32);
    TK_EQ_U64(tk, counts.task_shutdown, 32);
    threadpool_free(pool);
    TK_EQ_U64(tk, counts.worker_init, 2);
    TK_EQ_U64(tk, counts.worker_shutdown, 2);
}

static void t_smallmpqs_boundaries(tk_ctx *tk)
{
    mpz_t n;
    mpz_t expected;
    mpz_t product;
    mpz_t *factors;
    int num_factors;
    int i;

    mpz_init(n);
    mpz_init(expected);
    mpz_init(product);

    mpz_set_ui(n, 17);
    factors = smallmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -2);

    mpz_set_ui(n, 2 * 1048583UL);
    factors = smallmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -3);

    mpz_set_ui(n, 1);
    mpz_mul_2exp(n, n, 131);
    mpz_add_ui(n, n, 1);
    factors = smallmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -1);

    mpz_set_str(n, "1050809297549059047257", 10);
    mpz_set(expected, n);
    factors = smallmpqs(n, &num_factors);
    TK_REQUIRE(tk, factors != NULL && num_factors > 0,
               "smallmpqs failed on a valid odd composite");
    mpz_set_ui(product, 1);
    for (i = 0; i < num_factors; i++)
        mpz_mul(product, product, factors[i]);
    TK_CHECK(tk, mpz_cmp(product, expected) == 0);
    for (i = 0; i < num_factors; i++)
        mpz_clear(factors[i]);
    free(factors);

    mpz_clear(product);
    mpz_clear(expected);
    mpz_clear(n);
}

#if defined(USE_AVX512F)
extern void sort(uint64_t *data, uint32_t sz, int dir);
extern void sort32(uint32_t *data, uint32_t sz, int dir);

static int cmp_u64_asc(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int cmp_u64_desc(const void *a, const void *b)
{
    return -cmp_u64_asc(a, b);
}

static int cmp_u32_asc(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int cmp_u32_desc(const void *a, const void *b)
{
    return -cmp_u32_asc(a, b);
}

static void t_bitonic_arbitrary_lengths(tk_ctx *tk)
{
    static const uint32_t sizes[] = {0, 1, 2, 63, 64, 65, 100, 127, 128, 257};
    uint32_t si;
    int dir;
    int unaligned;

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    if (!__builtin_cpu_supports("avx512f"))
        return;
#endif
    for (si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        uint32_t n = sizes[si];
        for (dir = 0; dir <= 1; dir++) {
            for (unaligned = 0; unaligned <= 1; unaligned++) {
                unsigned char *raw64 = (unsigned char *)malloc(
                    (size_t)n * sizeof(uint64_t) + 128);
                unsigned char *raw32 = (unsigned char *)malloc(
                    (size_t)n * sizeof(uint32_t) + 128);
                uint64_t *got64;
                uint64_t *exp64;
                uint32_t *got32;
                uint32_t *exp32;
                uint32_t i;

                TK_REQUIRE(tk, raw64 != NULL && raw32 != NULL,
                           "sort test allocation failed");
                got64 = (uint64_t *)(((uintptr_t)raw64 + 63) & ~(uintptr_t)63);
                got32 = (uint32_t *)(((uintptr_t)raw32 + 63) & ~(uintptr_t)63);
                if (unaligned) {
                    got64 = (uint64_t *)((unsigned char *)got64 + sizeof(uint64_t));
                    got32 = (uint32_t *)((unsigned char *)got32 + sizeof(uint32_t));
                }
                exp64 = (uint64_t *)malloc(MAX(n, 1) * sizeof(uint64_t));
                exp32 = (uint32_t *)malloc(MAX(n, 1) * sizeof(uint32_t));
                TK_REQUIRE(tk, exp64 != NULL && exp32 != NULL,
                           "sort reference allocation failed");
                for (i = 0; i < n; i++) {
                    got64[i] = ((uint64_t)(n - i) << 33) ^ (i % 11);
                    got32[i] = (n - i) ^ (i % 7);
                }
                memcpy(exp64, got64, (size_t)n * sizeof(uint64_t));
                memcpy(exp32, got32, (size_t)n * sizeof(uint32_t));
                qsort(exp64, n, sizeof(uint64_t),
                      dir ? cmp_u64_desc : cmp_u64_asc);
                qsort(exp32, n, sizeof(uint32_t),
                      dir ? cmp_u32_desc : cmp_u32_asc);
                sort(got64, n, dir);
                sort32(got32, n, dir);
                TK_CHECKF(tk, memcmp(got64, exp64,
                          (size_t)n * sizeof(uint64_t)) == 0,
                          "64-bit sort mismatch: n=%u dir=%d unaligned=%d",
                          n, dir, unaligned);
                TK_CHECKF(tk, memcmp(got32, exp32,
                          (size_t)n * sizeof(uint32_t)) == 0,
                          "32-bit sort mismatch: n=%u dir=%d unaligned=%d",
                          n, dir, unaligned);
                free(exp32);
                free(exp64);
                free(raw32);
                free(raw64);
            }
        }
    }
}
#endif

static const tk_test tk__qs_review_tests[] = {
    { "threadpool_lifecycle", t_threadpool_lifecycle, "fast threads qs" },
    { "smallmpqs_boundaries", t_smallmpqs_boundaries, "fast mpqs qs" },
#if defined(USE_AVX512F)
    { "bitonic_arbitrary_lengths", t_bitonic_arbitrary_lengths,
      "fast avx512 qs" },
#endif
};

const tk_module tk_module_qs_review = {
    "qs_review",
    "QS support-code boundary and lifecycle regressions",
    tk__qs_review_tests,
    (int)(sizeof tk__qs_review_tests / sizeof tk__qs_review_tests[0])
};
