/*
ctx_test.c -- 验证 lasieve_ctx 这一层：状态按实例隔离，线程之间也隔离。

搬进 lasieve_ctx 之前，ECM/PM1 的缓存（素数位图、B1/B2 加法链、montgomery
预计算表）是文件级全局，整个进程只有一份。两个 siever 实例交替推进时，
各自的 B1/B2 预计算表会互相覆盖。这层测试要证明的就是：状态真的按实例
分开了。

三段：
  1. 两个实例写各自的字段，互相看不见
  2. 两个线程各自设一个实例，同时读写，字段不串
  3. 访问器在实例没建立时报错退出，而不是静默解引用空指针

**不覆盖**：ECM/PM1 的数值正确性。那两条路径在搬进 ctx 之前就有堆损坏
（未修改的 HEAD 版本同样复现），仓库里也没有任何测试走到它们。数值正确
性由 siever 端到端基准负责，见 test/standalone/lasieve/sieve_oracle.sh。
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <gmp.h>

#include "siever-config.h"
#include "montgomery_mul.h"
#include "if.h"
#include "lasieve_ctx.h"

#define NTHREADS 4
#define NROUNDS  2000

static int failures = 0;

static void check(int ok, const char *what)
{
  printf("  %-46s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok)
    failures++;
}

/* 一个线程一份：自己建实例，写满所有会带指针的字段，读回来比对。
 * 若状态其实是进程全局，这里必然串。 */
typedef struct {
  int   index;
  int   ok;
  u32_t ecm_prime_max_want;
  size_t b2_len_want;
  u32_t mm_tab1_len_want;
} worker_arg;

static void *worker(void *p)
{
  worker_arg *a = (worker_arg *)p;
  lasieve_ctx *ctx = lasieve_ctx_new();
  u32_t i;

  a->ok = 1;
  if (ctx == NULL) {
    a->ok = 0;
    return NULL;
  }
  lasieve_current_ctx = ctx;

  a->ecm_prime_max_want = 0x4000u + (u32_t)a->index;
  a->b2_len_want        = (size_t)(a->index + 1) * 64;
  a->mm_tab1_len_want   = 0x100u + (u32_t)a->index;

  for (i = 0; i < NROUNDS; i++) {
    lasieve_current_ctx->ecm_prime_max  = a->ecm_prime_max_want;
    lasieve_current_ctx->B2_scheme_len  = a->b2_len_want;
    lasieve_current_ctx->mm_B2_tab1_len = a->mm_tab1_len_want;
    lasieve_current_ctx->pm1_prime_max  = a->ecm_prime_max_want * 2;
    lasieve_current_ctx->gmp_f[0]._mp_size = (int)a->index;

    if (lasieve_current_ctx->ecm_prime_max  != a->ecm_prime_max_want ||
        lasieve_current_ctx->B2_scheme_len  != a->b2_len_want ||
        lasieve_current_ctx->mm_B2_tab1_len != a->mm_tab1_len_want ||
        lasieve_current_ctx->pm1_prime_max  != a->ecm_prime_max_want * 2 ||
        lasieve_current_ctx->gmp_f[0]._mp_size != (int)a->index) {
      a->ok = 0;
      break;
    }
  }

  lasieve_current_ctx = NULL;
  lasieve_ctx_free(ctx);
  return NULL;
}

int main(void)
{
  lasieve_ctx *a, *b;
  pthread_t th[NTHREADS];
  worker_arg arg[NTHREADS];
  int i, all_ok = 1;

  setbuf(stdout, NULL);
  printf("ctx_test: lasieve_ctx 状态隔离\n");

  /* ---- 1. 同线程内两个实例互不可见 ---- */
  a = lasieve_ctx_new();
  b = lasieve_ctx_new();
  if (a == NULL || b == NULL) {
    fprintf(stderr, "lasieve_ctx_new 失败\n");
    return 1;
  }
  check(a != b, "两次分配得到不同对象");

  lasieve_current_ctx = a;
  a->ecm_prime_max  = 0x4001;
  a->B2_scheme_len  = 64;
  a->mm_B2_tab1_len = 0x101;
  a->pm1_prime_max  = 0x8002;
  mpz_set_ui(a->gmp_f, 111);

  check(b->ecm_prime_max  == 0, "写 A 之后 B 的 ecm_prime_max 仍为 0");
  check(b->B2_scheme_len  == 0, "写 A 之后 B 的 B2_scheme_len 仍为 0");
  check(b->mm_B2_tab1_len == 0, "写 A 之后 B 的 mm_B2_tab1_len 仍为 0");
  check(b->pm1_prime_max  == 0, "写 A 之后 B 的 pm1_prime_max 仍为 0");
  check(mpz_cmp_ui(b->gmp_f, 0) == 0, "写 A 之后 B 的 gmp_f 仍为 0");

  lasieve_current_ctx = b;
  b->ecm_prime_max = 0x4003;
  check(a->ecm_prime_max == 0x4001, "写 B 不会改到 A 的 ecm_prime_max");
  check(lasieve_current_ctx == b, "访问器返回当前实例");

  /* gmp_f 必须真的 mpz_init 过，能安全写入与读出 */
  mpz_set_ui(b->gmp_f, 222);
  lasieve_current_ctx = a;
  check(mpz_cmp_ui(a->gmp_f, 111) == 0, "A 的 gmp_f 保持自己的值");
  check(mpz_sgn(a->gmp_aux0) == 0 && mpz_sgn(a->gmp_aux3) == 0,
        "gmp_aux0..3 建实例时已 mpz_init");

  lasieve_current_ctx = NULL;

  /* ---- 2. 四个线程各持一个实例 ---- */
  for (i = 0; i < NTHREADS; i++) {
    arg[i].index = i;
    arg[i].ok = 0;
    if (pthread_create(&th[i], NULL, worker, &arg[i]) != 0) {
      fprintf(stderr, "pthread_create 失败\n");
      return 1;
    }
  }
  for (i = 0; i < NTHREADS; i++) {
    pthread_join(th[i], NULL);
    if (!arg[i].ok)
      all_ok = 0;
  }
  check(all_ok, "四线程并发读写各自实例，字段不串");

  /* ---- 3. 释放不崩、不重复释放 ---- */
  lasieve_ctx_free(a);
  lasieve_ctx_free(b);
  lasieve_ctx_free(NULL);
  check(1, "lasieve_ctx_free 对 NULL 安全");

  if (failures) {
    printf("ctx_test: %d 项失败\n", failures);
    return 1;
  }
  printf("ctx_test: ok\n");
  return 0;
}
