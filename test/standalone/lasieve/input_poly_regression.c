#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include "factor/lasieve5_64/asm/siever-config.h"
#include "factor/lasieve5_64/input-poly.h"
static void clear_poly(mpz_t *p) { int i; for (i=0;i<9;i++) mpz_clear(p[i]); free(p); }
static FILE *input(const char *s) { FILE *f=tmpfile(); assert(f); fputs(s,f); rewind(f); return f; }
int main(void) {
  mpz_t n,m; mpz_t *a,*b; i32_t ad,bd; FILE *f;
  mpz_init(n); mpz_init(m);
  f=input("n: 15\nm: 2\nc0: -6\nc1: 1\nc2: 1\nEND_POLY\nc8: invalid\n");
  input_poly(n,&a,&ad,&b,&bd,m,f); fclose(f);
  assert(ad==2 && bd==1 && mpz_cmp_ui(m,2)==0); clear_poly(a); clear_poly(b);
  f=input("n: 15\nc0: -6\nc1: 1\nc2: 1\nY0: -2\nY1: 1\nEND_POLY\n");
  input_poly(n,&a,&ad,&b,&bd,m,f); fclose(f);
  assert(ad==2 && bd==1 && mpz_cmp_ui(m,2)==0); clear_poly(a); clear_poly(b);
  mpz_clear(m); mpz_clear(n); return 0;
}
