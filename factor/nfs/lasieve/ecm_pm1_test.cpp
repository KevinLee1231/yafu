/*
ecm_pm1_test.c -- ECM 与 P−1 的数值正确性，以及两个实例交错推进的隔离性。

三件事：

1. 这两条路径的位图访问曾经按字节下标算，而缓冲区按 u64 字分配，越界到
   缓冲区外 8 倍处。ECM 和 PM1 各有一份，症状是第一条曲线就
   heap-buffer-overflow；仓库里没有任何测试走到它们，所以一直没被发现。
   这里直接检查它们能不能真的把一个已知合数拆开。

2. 状态搬进 lasieve_ctx 之后，正确的判据不是"没崩"，而是两个实例交错推进
   曲线的结果和各自单独跑完全一样。两边用不同的 B1/B2，强制走不同的
   加法链、不同的 B2 scheme 表项、不同的 montgomery 预计算、不同的素数
   位图长度——如果这些还是进程全局，交错的结果就会和单独跑对不上。

3. **两个实例必须用同一个 N。** set_montgomery_multiplication() 写的
   montgomery 状态（montgomery_ulongs / montgomery_inv_n / modulo_n）还是
   进程级全局，而且汇编按符号名直接读它，不在 lasieve_ctx 里。同一个 N 下
   这份状态对两个实例本来就相同，交错才有意义；换不同的 N，A 的曲线会跑在
   B 的 montgomery 状态下，算出来是错的。这不是 ctx 的缺陷，是剩下的
   汇编边界，见 lasieve/README.md。
*/

#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>

#include "siever-config.h"
#include "montgomery_mul.h"
#include "if.h"
#include "ecm.h"
#include "pm1.h"
#include "lasieve_ctx.h"

/* 单份对象链的是 lasieve_single 这一份（lasieve_ns.h 的默认值），
 * 而 per-I 的名字现在都在命名空间里，所以这里要把它引进来。 */
using namespace lasieve_ns;

#define MAX_CURVES 400

static int failures = 0;

static void check(int ok, const char* what)
{
  printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok)
    failures++;
}

/* 1000003 * 1000033，两个 10^6 级素数之积。ECM 与 PM1 都应该能拆开。
 * 两个实例共用它，见文件头第 3 点。 */
static const char *N_SHARED = "1000036000099";

/* 参数刻意不同：B1 差一档，B2 差一个数量级 */
static const u32_t B1_A =  2000, B2_A = 100000;
static const u32_t B1_B = 11000, B2_B = 187000;

/* 推进一条曲线。返回 1 拿到真因子，0 这条曲线没找到，-1 出错。 */
static int ecm_advance_one(ecm_t e, mpz_t n, mpz_t out)
{
  mpz_t *f = NULL;
  int rc = ecm(e, &f);

  if (rc < 0)
    return -1;
  if ((rc == 1) && (mpz_cmp_ui(*f, 1) > 0) && (mpz_cmp(*f, n) < 0))
  {
    mpz_set(out, *f);
    return 1;
  }
  /* 返回 N：这条曲线没找到因子，换下一条 */
  return 0;
}

/* 单独跑一个实例到底 */
static int ecm_solo(mpz_t n, u32_t b1, u32_t b2, mpz_t out, u32_t *curves)
{
  ecm_t e;
  int r;

  ecm_curve_init(e);
  if (ecm_curve_set(e, n, b1, b2))
  {
    ecm_curve_clear(e);
    return -1;
  }
  for (*curves = 0; *curves < MAX_CURVES; (*curves)++)
  {
    r = ecm_advance_one(e, n, out);
    if (r != 0)
    {
      (*curves)++;
      ecm_curve_clear(e);
      return r;
    }
  }
  ecm_curve_clear(e);
  return 0;
}

/* PM1 也要能返回 N（累积值整体整除 N），那种情况当作没找到 */
static int pm1_once(mpz_t n, u32_t b1, u32_t b2, mpz_t out)
{
  mpz_t *f = NULL;

  if (pm1_factor(n, b1, b2, &f) == 0)
    return 0;
  if (f == NULL)
    return -1;
  if ((mpz_cmp_ui(*f, 1) <= 0) || (mpz_cmp(*f, n) >= 0))
    return 0;
  mpz_set(out, *f);
  return 1;
}

static int is_proper_factor(mpz_t f, mpz_t n)
{
  if (mpz_cmp_ui(f, 1) <= 0)
    return 0;
  if (mpz_cmp(f, n) >= 0)
    return 0;
  return mpz_divisible_p(n, f) != 0;
}

int main(void)
{
  mpz_t n;
  mpz_t soloA, soloB, mixA, mixB, pmA, pmB;
  u32_t curvesA_solo = 0, curvesB_solo = 0;
  u32_t curvesA_mix = 0, curvesB_mix = 0;
  ecm_t eA, eB;
  int ra, rb, rA, rB, step;
  lasieve_ctx *ctxA, *ctxB;

  mpz_init_set_str(n, N_SHARED, 10);
  mpz_inits(soloA, soloB, mixA, mixB, pmA, pmB, NULL);

  setbuf(stdout, NULL);
  printf("ecm_pm1_test: ECM/P-1 正确性与多实例隔离\n");

  ctxA = lasieve_ctx_new();
  ctxB = lasieve_ctx_new();
  if ((ctxA == NULL) || (ctxB == NULL))
  {
    fprintf(stderr, "lasieve_ctx_new 失败\n");
    return 1;
  }

  /* ---- 1. 各自单独跑到底 ---- */
  lasieve_current_ctx = ctxA;
  ra = ecm_solo(n, B1_A, B2_A, soloA, &curvesA_solo);
  check(ra == 1, "ECM 在实例 A 上拿到真因子");
  check((ra != 1) || is_proper_factor(soloA, n), "ECM A 的因子整除 N 且非平凡");

  lasieve_current_ctx = ctxB;
  rb = ecm_solo(n, B1_B, B2_B, soloB, &curvesB_solo);
  check(rb == 1, "ECM 在实例 B 上拿到真因子");
  check((rb != 1) || is_proper_factor(soloB, n), "ECM B 的因子整除 N 且非平凡");

  /* 参数不同、用到的曲线数应当也不同，否则交错测试说明不了问题 */
  if ((ra == 1) && (rb == 1) && (curvesA_solo == curvesB_solo))
    printf("  注意：A 与 B 都用了 %u 条曲线，参数差异可能不够明显\n", curvesA_solo);

  /* ---- 2. 交错推进 ---- */
  lasieve_current_ctx = ctxA;
  ecm_curve_init(eA);
  if (ecm_curve_set(eA, n, B1_A, B2_A))
  {
    fprintf(stderr, "ecm_curve_set(A) 失败\n");
    return 1;
  }
  lasieve_current_ctx = ctxB;
  ecm_curve_init(eB);
  if (ecm_curve_set(eB, n, B1_B, B2_B))
  {
    fprintf(stderr, "ecm_curve_set(B) 失败\n");
    return 1;
  }

  /* 轮流推进：每次 ecm() 之前切换当前实例，两边的缓存因此真的被交叉使用。
   * 若这些缓存还是进程全局，先完成的那一个会覆盖另一个的 B1/B2 表。 */
  rA = rB = 0;
  for (step = 0; step < 2 * MAX_CURVES; step++)
  {
    if (rA == 0)
    {
      lasieve_current_ctx = ctxA;
      rA = ecm_advance_one(eA, n, mixA);
      curvesA_mix++;
    }
    if (rA < 0)
      break;
    if (rB == 0)
    {
      lasieve_current_ctx = ctxB;
      rB = ecm_advance_one(eB, n, mixB);
      curvesB_mix++;
    }
    if (rB < 0)
      break;
    if ((rA == 1) && (rB == 1))
      break;
  }
  ecm_curve_clear(eA);
  ecm_curve_clear(eB);

  check(rA == 1, "交错推进后 A 仍拿到真因子");
  check(rB == 1, "交错推进后 B 仍拿到真因子");
  check((rA == 1) && is_proper_factor(mixA, n), "交错后 A 的因子整除 N 且非平凡");
  check((rB == 1) && is_proper_factor(mixB, n), "交错后 B 的因子整除 N 且非平凡");
  check((ra == 1) && (rA == 1) && (curvesA_solo == curvesA_mix),
        "交错后 A 的曲线数与单独跑一致");
  check((rb == 1) && (rB == 1) && (curvesB_solo == curvesB_mix),
        "交错后 B 的曲线数与单独跑一致");
  check((ra == 1) && (rA == 1) && (mpz_cmp(soloA, mixA) == 0),
        "交错后 A 的因子与单独跑一致");
  check((rb == 1) && (rB == 1) && (mpz_cmp(soloB, mixB) == 0),
        "交错后 B 的因子与单独跑一致");

  /* ---- 3. PM1：与 ECM 共用同一份 gmp_f，是共享关系最紧的一处 ----
   * 两组边界都取实测能拿到真因子的（B1=100000 与 130000 都拆出 1000033），
   * 但走的 B1/B2 scheme 表项不同。 */
  lasieve_current_ctx = ctxA;
  check(pm1_once(n, 100000, 1000000, pmA) == 1, "PM1 在实例 A 上拿到真因子");
  check(is_proper_factor(pmA, n), "PM1 A 的因子整除 N 且非平凡");

  lasieve_current_ctx = ctxB;
  check(pm1_once(n, 130000, 1300000, pmB) == 1, "PM1 在实例 B 上拿到真因子");
  check(is_proper_factor(pmB, n), "PM1 B 的因子整除 N 且非平凡");

  lasieve_current_ctx = NULL;
  lasieve_ctx_free(ctxA);
  lasieve_ctx_free(ctxB);
  mpz_clears(n, soloA, soloB, mixA, mixB, pmA, pmB, NULL);

  if (failures)
  {
    printf("ecm_pm1_test: %d 项失败\n", failures);
    return 1;
  }
  printf("ecm_pm1_test: ok\n");
  return 0;
}
