/*----------------------------------------------------------------------
 Layer 3 -- targeted regressions for shared QS support code.
----------------------------------------------------------------------*/
#include "testkit.h"
/* nfs_impl.h 要先整体解析：它自己的 include 顺序在 nfs.cpp 里是能编过的。
   若先 include 别的头（thread.h 等）把 util.h 拉进来，HAS_SSE2 会先被定义
   （无值），factor.h 里的 "int HAS_SSE2;" 就成了 "int ;"。 */
#include "nfs_impl.h"
#include "qs_impl.h"
#include "thread.h"
#include "pmpqs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t parse_job_file(fact_obj_t *fobj, nfs_job_t *job);
void fill_job_file(fact_obj_t *fobj, nfs_job_t *job, uint32_t missing_params);

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

static void t_pmpqs_boundaries(tk_ctx *tk)
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
    factors = pmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -2);

    mpz_set_ui(n, 2 * 1048583UL);
    factors = pmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -3);

    mpz_set_ui(n, 1);
    mpz_mul_2exp(n, n, 131);
    mpz_add_ui(n, n, 1);
    factors = pmpqs(n, &num_factors);
    TK_CHECK(tk, factors == NULL && num_factors == -1);

    mpz_set_str(n, "1050809297549059047257", 10);
    mpz_set(expected, n);
    factors = pmpqs(n, &num_factors);
    TK_REQUIRE(tk, factors != NULL && num_factors > 0,
               "pmpqs failed on a valid odd composite");
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


/* 块大小 qrange 的落盘与读回。
 *
 * 已筛区间是按这个大小划的，所以续算必须沿用同一个值。写进 .job 文件，
 * 读的时候取第一处 —— 续算时追加的那一行不会覆盖原来的。 */
static void t_nfs_qrange_roundtrip(tk_ctx *tk)
{
    fact_obj_t fobj;
    nfs_job_t job;
    char path[512];
    char line[512];
    FILE *fp;

    memset(&fobj, 0, sizeof(fobj));
    memset(&job, 0, sizeof(job));
    snprintf(fobj.nfs_obj.job_infile, sizeof(fobj.nfs_obj.job_infile),
        "yafu_test_qrange.job");
    snprintf(fobj.flogname, sizeof(fobj.flogname), "yafu_test_qrange.log");

    /* 文件里有 qrange，读回来应当就是它 */
    fp = fopen(fobj.nfs_obj.job_infile, "w");
    TK_REQUIRE(tk, fp != NULL, "cannot create %s", fobj.nfs_obj.job_infile);
    fputs("qrange: 12345\n", fp);
    fclose(fp);

    parse_job_file(&fobj, &job);
    TK_EQ_U64(tk, job.qrange, 12345);

    /* fill_job_file 应当把 qrange 写进去（是追加写，所以逐行找） */
    {
        int found = 0;

        job.qrange = 999;
        fill_job_file(&fobj, &job, 0);
        fp = fopen(fobj.nfs_obj.job_infile, "r");
        TK_REQUIRE(tk, fp != NULL, "cannot reopen %s", fobj.nfs_obj.job_infile);
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, "qrange: 999") != NULL)
                found = 1;
        }
        fclose(fp);
        TK_CHECK(tk, found);
    }

    /* 追加写的约定：同一文件里多行同一个关键字时，最后一行生效
       （lpbr/alim 等都是这样）。续算时 nfs.cpp 用文件里的值覆盖查表值，
       追加的那一行因此始终是同一个值。 */
    memset(&job, 0, sizeof(job));
    parse_job_file(&fobj, &job);
    TK_EQ_U64(tk, job.qrange, 999);

    /* 往返一致：写进去什么，读回来就是什么 */
    job.qrange = 4096;
    fill_job_file(&fobj, &job, 0);
    memset(&job, 0, sizeof(job));
    parse_job_file(&fobj, &job);
    TK_EQ_U64(tk, job.qrange, 4096);

    remove(fobj.nfs_obj.job_infile);
    remove(fobj.flogname);
}


/* SIQS 的 B 值序列。
 *
 * b 值一共有 1<<(s-1) 个（Gray 码的总数），相邻两步的差是
 * ±Bl[ctz(i)]。这里检查整条序列，特别是最后一个槽 —— 早先
 * generate_bpolys 的循环只填了 curr_b[0..maxB-2]，最后一个槽一直是
 * 未初始化的内存，而数据文件里的 B-poly 索引可以合法地等于 maxB-1。 */
static void t_siqs_bpoly_sequence(tk_ctx *tk)
{
    const int s = 4;
    const int maxB = 1 << (s - 1);
    siqs_poly *poly;
    dynamic_conf_t *dconf;
    static_conf_t *sconf;
    mpz_t diff, absdiff;
    int j;

    poly = (siqs_poly *)malloc(sizeof(*poly));
    dconf = (dynamic_conf_t *)malloc(sizeof(*dconf));
    sconf = (static_conf_t *)malloc(sizeof(*sconf));
    TK_REQUIRE(tk, poly && dconf && sconf, "out of memory");

    memset(poly, 0, sizeof(*poly));
    memset(dconf, 0, sizeof(*dconf));
    memset(sconf, 0, sizeof(*sconf));

    poly->s = s;
    poly->gray = (char *)malloc((size_t)maxB);
    poly->nu = (char *)malloc((size_t)maxB);
    TK_REQUIRE(tk, poly->gray && poly->nu, "out of memory");
    mpz_init(poly->mpz_poly_a);
    mpz_init(poly->mpz_poly_b);
    mpz_init(poly->mpz_poly_c);
    mpz_set_ui(poly->mpz_poly_b, 100);
    get_gray_code(poly);

    dconf->curr_poly = poly;
    dconf->Bl = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)s);
    TK_REQUIRE(tk, dconf->Bl != NULL, "out of memory");
    for (j = 0; j < s; j++) {
        mpz_init(dconf->Bl[j]);
        mpz_set_ui(dconf->Bl[j], (unsigned)(j + 1) * 10);
    }
    dconf->numB = 0;

    mpz_init(sconf->n);
    mpz_set_ui(sconf->n, 1000000);
    sconf->knmod8 = 0;                 /* 不做 b 的奇偶修正 */
    sconf->curr_b = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)maxB);
    TK_REQUIRE(tk, sconf->curr_b != NULL, "out of memory");
    for (j = 0; j < maxB; j++)
        mpz_init(sconf->curr_b[j]);
    sconf->bpoly_alloc = (uint32_t)maxB;

    generate_bpolys(sconf, dconf, maxB);

    /* 第一个就是初始 b */
    TK_EQ_U64(tk, mpz_get_ui(sconf->curr_b[0]), 100);

    /* 相邻两步的差是 ±Bl[ctz(i)] */
    mpz_init(diff);
    mpz_init(absdiff);
    for (j = 1; j < maxB; j++) {
        unsigned v = (unsigned)j;
        int ctz = 0;

        while ((v & 1) == 0) {
            ctz++;
            v >>= 1;
        }
        mpz_sub(diff, sconf->curr_b[j], sconf->curr_b[j - 1]);
        mpz_abs(absdiff, diff);
        TK_CHECKF(tk, mpz_cmp(absdiff, dconf->Bl[ctz]) == 0,
            "B value step %d: difference is not +-Bl[%d]", j, ctz);
    }
    mpz_clear(diff);
    mpz_clear(absdiff);

    for (j = 0; j < maxB; j++)
        mpz_clear(sconf->curr_b[j]);
    free(sconf->curr_b);
    mpz_clear(sconf->n);
    for (j = 0; j < s; j++)
        mpz_clear(dconf->Bl[j]);
    free(dconf->Bl);
    mpz_clear(poly->mpz_poly_a);
    mpz_clear(poly->mpz_poly_b);
    mpz_clear(poly->mpz_poly_c);
    free(poly->gray);
    free(poly->nu);
    free(poly);
    free(dconf);
    free(sconf);
}


/* 按需生成与一次性生成必须逐值一致。
 *
 * generate_bpolys 一次把 maxB 个 'b' 值都算出来，ensure_bpoly 按需算到
 * 指定的序号。后者是前者的增量版本，两者给出的序列必须完全一样 ——
 * 否则筛法用的 'b' 值会跟原来不同，关系就全错了。 */
static void t_siqs_lazy_bpoly_matches(tk_ctx *tk)
{
    const int s = 5;
    const int maxB = 1 << (s - 1);
    siqs_poly *poly;
    dynamic_conf_t *dconf;
    static_conf_t *sconf;
    mpz_t *all;
    int j;

    poly = (siqs_poly *)malloc(sizeof(*poly));
    dconf = (dynamic_conf_t *)malloc(sizeof(*dconf));
    sconf = (static_conf_t *)malloc(sizeof(*sconf));
    TK_REQUIRE(tk, poly && dconf && sconf, "out of memory");

    memset(poly, 0, sizeof(*poly));
    memset(dconf, 0, sizeof(*dconf));
    memset(sconf, 0, sizeof(*sconf));

    poly->s = s;
    poly->gray = (char *)malloc((size_t)(1 << (MAX_A_FACTORS - 1)));
    poly->nu = (char *)malloc((size_t)(1 << (MAX_A_FACTORS - 1)));
    TK_REQUIRE(tk, poly->gray && poly->nu, "out of memory");
    mpz_init(poly->mpz_poly_a);
    mpz_init(poly->mpz_poly_b);
    mpz_init(poly->mpz_poly_c);
    mpz_set_ui(poly->mpz_poly_a, 3);
    mpz_set_ui(poly->mpz_poly_b, 100);
    get_gray_code(poly);

    dconf->curr_poly = poly;
    dconf->Bl = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)s);
    TK_REQUIRE(tk, dconf->Bl != NULL, "out of memory");
    for (j = 0; j < s; j++) {
        mpz_init(dconf->Bl[j]);
        mpz_set_ui(dconf->Bl[j], (unsigned)(j + 1) * 10);
    }
    dconf->numB = 0;

    mpz_init(sconf->n);
    mpz_set_ui(sconf->n, 1000000);
    sconf->knmod8 = 0;                 /* 不做 b 的奇偶修正 */
    sconf->curr_b = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)maxB);
    TK_REQUIRE(tk, sconf->curr_b != NULL, "out of memory");
    for (j = 0; j < maxB; j++)
        mpz_init(sconf->curr_b[j]);
    sconf->bpoly_alloc = (uint32_t)maxB;

    /* 路径一：一次性生成 */
    all = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)maxB);
    TK_REQUIRE(tk, all != NULL, "out of memory");
    for (j = 0; j < maxB; j++)
        mpz_init(all[j]);
    sconf->curr_b = all;
    generate_bpolys(sconf, dconf, maxB);
    /* generate_bpolys 填的是 curr_b[0..maxB-1] */

    /* 路径二：按需生成（乱序访问，验证它能补算到任意序号） */
    sconf->curr_b = (mpz_t *)malloc(sizeof(mpz_t) * (size_t)maxB);
    TK_REQUIRE(tk, sconf->curr_b != NULL, "out of memory");
    for (j = 0; j < maxB; j++)
        mpz_init(sconf->curr_b[j]);
    /* 路径一的 nextB 改过 poly->mpz_poly_b，重置回初值 */
    mpz_set_ui(poly->mpz_poly_b, 100);

    sconf->bpoly_gray = poly->gray;
    sconf->bpoly_nu = poly->nu;
    sconf->bpoly_Bl = dconf->Bl;
    mpz_init(sconf->bpoly_a);
    mpz_init(sconf->bpoly_b);
    mpz_set(sconf->bpoly_a, poly->mpz_poly_a);
    mpz_set(sconf->bpoly_b, poly->mpz_poly_b);
    sconf->bpoly_next = 0;
    sconf->bpoly_numB = 1;
    sconf->bpoly_inited = 1;

    {
        /* 乱序访问，验证按需生成能补算到任意序号 */
        static const int order[] = {5, 0, 3, 1, 7, 2, 6, 4,
                                   9, 8, 11, 10, 13, 12, 15, 14};
        int k;

        for (k = 0; k < maxB; k++) {
            int idx = order[k];

            ensure_bpoly(sconf, (uint32_t)idx);
            TK_CHECKF(tk, mpz_cmp(sconf->curr_b[idx], all[idx]) == 0,
                "lazy B value %d differs from the eagerly generated one", idx);
        }
    }

    for (j = 0; j < maxB; j++) {
        mpz_clear(sconf->curr_b[j]);
        mpz_clear(all[j]);
    }
    free(sconf->curr_b);
    free(all);
    mpz_clear(sconf->bpoly_a);
    mpz_clear(sconf->bpoly_b);
    mpz_clear(sconf->n);
    for (j = 0; j < s; j++)
        mpz_clear(dconf->Bl[j]);
    free(dconf->Bl);
    mpz_clear(poly->mpz_poly_a);
    mpz_clear(poly->mpz_poly_b);
    mpz_clear(poly->mpz_poly_c);
    free(poly->gray);
    free(poly->nu);
    free(poly);
    free(dconf);
    free(sconf);
}

/* process_rel 的截断检查。
 *
 * 数据文件被截断（进程被 kill）时，最后一行可能只剩半截。原来只有
 * lp[0] 做了"没东西可解析"的检查，lp[1] 没有 —— 它会静默变成 0，
 * 而 0 会被当成一个真的大素数加进 cycle 图，图里多出一个素数 0 的
 * 顶点，环计数随之偏差。这次给 lp[1] 也补上了检查。
 *
 * 判据用哨兵值：rel->large_prime[1] 预置成 0xDEADBEEF。截断的行在
 * 早退时不会碰它（赋值在所有截断检查之后），而没被截断的行会走到
 * 那里写上真正的 lp[1]。所以"哨兵保持不变"就说明这行被当成损坏
 * 数据拒掉了。
 *
 * 返回值区分不了"因截断被拒"和"因关系校验失败被拒"——两者都是 1，
 * 所以不能只看返回值。
 */

#define PROC_REL_SENTINEL 0xDEADBEEFu

static void t_siqs_process_rel_truncated(tk_ctx *tk)
{
    static_conf_t *sconf;
    fb_list fb;
    fb_element_siqs *fbel;
    siqs_r rel;
    mpz_t n;
    siqs_poly poly;
    fact_obj_t obj;
    int rc;
    static const uint32_t primes[] = {2, 3, 5, 7, 11, 13, 17, 19};

    memset(&fb, 0, sizeof(fb));
    memset(&poly, 0, sizeof(poly));
    /* process_rel 走到最后会调 check_relation(..., obj->VFLAG, ...)，
     * obj 传 NULL 就是段错误。清零的 fact_obj_t 够用。 */
    memset(&obj, 0, sizeof(obj));

    /* 一个最小的因子基：小素数列表，够 process_rel 的试除循环用 */
    fbel = (fb_element_siqs *)calloc(sizeof(fbel[0]),
                                    sizeof(primes) / sizeof(primes[0]));
    TK_REQUIRE(tk, fbel != NULL, "out of memory");
    fbel->prime = (uint32_t *)calloc(sizeof(primes) / sizeof(primes[0]),
                                     sizeof(uint32_t));
    TK_REQUIRE(tk, fbel->prime != NULL, "out of memory");
    memcpy(fbel->prime, primes, sizeof(primes));
    fb.list = fbel;
    fb.B = (uint32_t)(sizeof(primes) / sizeof(primes[0]));

    sconf = (static_conf_t *)calloc(1, sizeof(static_conf_t));
    TK_REQUIRE(tk, sconf != NULL, "out of memory");

    mpz_init_set_ui(n, 1000003);
    mpz_init_set_ui(sconf->curr_a, 2);
    mpz_init_set_ui(poly.mpz_poly_b, 2);
    poly.s = 1;
    poly.qlisort = (int *)calloc(1, sizeof(int));
    TK_REQUIRE(tk, poly.qlisort != NULL, "out of memory");
    poly.qlisort[0] = 0;

    sconf->knmod8 = 0;                 /* 不做 b 的奇偶修正 */
    sconf->num_lp = 2;
    sconf->factor_base = &fb;
    sconf->curr_poly = &poly;
    sconf->sieve_small_fb_start = 1;   /* 小素数试除从下标 1 开始 */
    sconf->curr_b = (mpz_t *)calloc(4, sizeof(mpz_t));
    TK_REQUIRE(tk, sconf->curr_b != NULL, "out of memory");
    for (rc = 0; rc < 4; rc++)
        mpz_init_set_ui(sconf->curr_b[rc], 2);
    /* process_rel 会检查 this_id >= sconf->bpoly_alloc，不设的话所有用例
     * 都在这一个检查上早退，测不到大素数的解析。 */
    sconf->bpoly_alloc = 4;
    /* process_rel 解析出 this_id 之后会调 ensure_bpoly，它要读 bpoly_b /
     * bpoly_gray / bpoly_nu / bpoly_Bl。这些不准备就是段错误。 */
    sconf->bpoly_gray = (char *)calloc(4, 1);
    sconf->bpoly_nu = (char *)calloc(4, 1);
    sconf->bpoly_Bl = (mpz_t *)calloc(4, sizeof(mpz_t));
    TK_REQUIRE(tk, sconf->bpoly_gray && sconf->bpoly_nu && sconf->bpoly_Bl,
        "out of memory");
    for (rc = 0; rc < 4; rc++) {
        mpz_init_set_ui(sconf->bpoly_Bl[rc], 1);
        sconf->bpoly_gray[rc] = 1;
        sconf->bpoly_nu[rc] = 1;
    }
    mpz_init_set_ui(sconf->bpoly_a, 2);
    mpz_init_set_ui(sconf->bpoly_b, 2);
    sconf->bpoly_numB = 1;
    sconf->bpoly_next = 1;       /* 第 0 个 b 值已经算好 */

    /* 1) 截断在 'L' 之前：连大素数都没有 */
    {
        char line[] = "10 0 2 3 L";
        memset(&rel, 0, sizeof(rel));
        rel.large_prime[1] = PROC_REL_SENTINEL;
        rc = process_rel(line, &fb, n, sconf, &obj, &rel);
        TK_CHECKF(tk, rc == 1, "truncated before lp[0]: expected 1, got %d", rc);
        TK_CHECKF(tk, rel.large_prime[1] == PROC_REL_SENTINEL,
            "truncated before lp[0] was not rejected early");
    }

    /* 2) 截断在 lp[0] 与 lp[1] 之间：只有一个大素数。
     *    这一档是本次修复针对的 —— 旧代码会让 lp[1] 静默变成 0。 */
    {
        char line[] = "10 0 2 3 L 11";
        memset(&rel, 0, sizeof(rel));
        rel.large_prime[1] = PROC_REL_SENTINEL;
        rc = process_rel(line, &fb, n, sconf, &obj, &rel);
        TK_CHECKF(tk, rc == 1,
            "truncated between lp[0] and lp[1]: expected 1, got %d", rc);
        TK_CHECKF(tk, rel.large_prime[1] == PROC_REL_SENTINEL,
            "truncated between lp[0] and lp[1] was not rejected early "
            "(lp[1] silently became %u)", rel.large_prime[1]);
    }

    /* 3) 截断在因子列表中间 */
    {
        char line[] = "10 0 2";
        memset(&rel, 0, sizeof(rel));
        rel.large_prime[1] = PROC_REL_SENTINEL;
        rc = process_rel(line, &fb, n, sconf, &obj, &rel);
        TK_CHECKF(tk, rc == 1, "truncated in factor list: expected 1, got %d", rc);
        TK_CHECKF(tk, rel.large_prime[1] == PROC_REL_SENTINEL,
            "truncated factor list was not rejected early");
    }

    /* 4) 截断在偏移/因子之间：'L' 之前就没了 */
    {
        char line[] = "10 0";
        memset(&rel, 0, sizeof(rel));
        rel.large_prime[1] = PROC_REL_SENTINEL;
        rc = process_rel(line, &fb, n, sconf, &obj, &rel);
        TK_CHECKF(tk, rc == 1, "truncated after factors: expected 1, got %d", rc);
        TK_CHECKF(tk, rel.large_prime[1] == PROC_REL_SENTINEL,
            "truncated after factors was not rejected early");
    }

    for (rc = 0; rc < 4; rc++) {
        mpz_clear(sconf->curr_b[rc]);
        mpz_clear(sconf->bpoly_Bl[rc]);
    }
    free(sconf->curr_b);
    free(sconf->bpoly_gray);
    free(sconf->bpoly_nu);
    free(sconf->bpoly_Bl);
    mpz_clear(sconf->bpoly_a);
    mpz_clear(sconf->bpoly_b);
    free(poly.qlisort);
    mpz_clear(poly.mpz_poly_b);
    mpz_clear(sconf->curr_a);
    mpz_clear(n);
    free(fbel->prime);
    free(fbel);
    free(sconf);
}

static const tk_test tk__qs_review_tests[] = {
    { "threadpool_lifecycle", t_threadpool_lifecycle, "fast threads qs" },
    { "pmpqs_boundaries", t_pmpqs_boundaries, "fast mpqs qs" },
    { "nfs_qrange_roundtrip", t_nfs_qrange_roundtrip, "fast nfs qs" },
    { "siqs_bpoly_sequence", t_siqs_bpoly_sequence, "fast siqs qs" },
    { "siqs_lazy_bpoly_matches", t_siqs_lazy_bpoly_matches, "fast siqs qs" },
    { "siqs_process_rel_truncated", t_siqs_process_rel_truncated, "fast siqs qs" },
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
