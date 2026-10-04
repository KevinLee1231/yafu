/* lasieve_ctx.c -- 实例的分配与释放，以及当前实例的 thread-local 指针 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#include "lasieve_ctx.h"

__thread lasieve_ctx *lasieve_current_ctx = NULL;

lasieve_ctx *lasieve_ctx_current(void)
{
  if (lasieve_current_ctx == NULL) {
    fputs("lasieve: 还没有建立 siever 实例"
          "（lasieve_current_ctx 为 NULL）\n", stderr);
    abort();
  }
  return lasieve_current_ctx;
}

lasieve_ctx *lasieve_ctx_new(void)
{
  lasieve_ctx *ctx = (lasieve_ctx *)calloc(1, sizeof(lasieve_ctx));
  if (ctx == NULL)
    return NULL;
  mpz_init(ctx->gmp_f);
  mpz_init(ctx->gmp_aux0);
  mpz_init(ctx->gmp_aux1);
  mpz_init(ctx->gmp_aux2);
  mpz_init(ctx->gmp_aux3);
  return ctx;
}

void lasieve_ctx_free(lasieve_ctx *ctx)
{
  if (ctx == NULL)
    return;
  free(ctx->ecm_prime_bit);
  free(ctx->addition_chain);
  free(ctx->B1_ac);
  free(ctx->B1_prime);
  free(ctx->B2_scheme);
  free(ctx->mm_stack);
  free(ctx->mm_B1_x);
  free(ctx->mm_B1_z);
  free(ctx->mm_B2_inv);
  free(ctx->mm_B2_tab1);
  free(ctx->mm_B2_tab2);
  free(ctx->pm1_prime_bit);
  free(ctx->B1_scheme);
  free(ctx->pm1_B2_scheme);
  free(ctx->pm1_mm_stack);
  free(ctx->pm1_mm_B2_tab1);
  free(ctx->pm1_mm_B2_tab2);
  mpz_clear(ctx->gmp_f);
  mpz_clear(ctx->gmp_aux0);
  mpz_clear(ctx->gmp_aux1);
  mpz_clear(ctx->gmp_aux2);
  mpz_clear(ctx->gmp_aux3);
  free(ctx);
}
