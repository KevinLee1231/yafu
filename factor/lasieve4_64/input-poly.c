/* input-poly.c
  By Jens Franke.
  6/13/04: Hacked up for use in GGNFS by Chris Monico.
                                                                                                                                                                                                             
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
                                                                                                                                                                                                             
  You should have received a copy of the GNU General Public License along
  with this program; see the file COPYING.  If not, write to the Free
  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <gmp.h>

#include "asm/siever-config.h"
#include "if.h"
#define MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#ifdef _OLD_FORMAT
/*******************************************************/
void input_poly(mpz_t N, mpz_t ** A, i32_t * adeg_ptr, mpz_t ** B,
                i32_t * bdeg_ptr, mpz_t m, FILE * input_file)
/*******************************************************/
{
  char *input_line = NULL;
  size_t input_line_alloc = 0;
  i32_t have_m = 0;

  if (mpz_inp_str(N, input_file, 10) == 0)
    complain("Cannot read number which is to be factored: %m\n");

  *adeg_ptr = -1;
  *bdeg_ptr = -1;
  while (have_m == 0) {
    i32_t grad;
    char *field;

    if (skip_blanks_comments(&input_line, &input_line_alloc, input_file) <= 0)
      complain
        ("Cannot read common root of NFS polynomials from input file\n");
    switch (*input_line) {
      case 'X':
        if (sscanf(input_line + 1, "%d", &grad) == 0)
          complain("Cannot understand input line %s\n", input_line);
        if (grad > *adeg_ptr) {
          i32_t i;

          if (*adeg_ptr >= 0)
            *A = xrealloc(*A, (grad + 1) * sizeof(**A));
          else
            *A = xmalloc((grad + 1) * sizeof(**A));
          for (i = *adeg_ptr + 1; i <= grad; i++)
            mpz_init_set_ui((*A)[i], 0);
          *adeg_ptr = grad;
        }
        strtok(input_line, " \t");
        field = strtok(NULL, " \t");
        if (string2mpz((*A)[grad], field, 10) != 0)
          complain("Cannot understand number %s\n", field);
        break;
      case 'Y':
        if (sscanf(input_line + 1, "%d", &grad) == 0)
          complain("Cannot understand input line %s\n", input_line);
        if (grad > *bdeg_ptr) {
          i32_t i;

          if (*bdeg_ptr >= 0)
            *B = xrealloc(*B, (grad + 1) * sizeof(**B));
          else
            *B = xmalloc((grad + 1) * sizeof(**B));
          for (i = *bdeg_ptr + 1; i <= grad; i++)
            mpz_init_set_ui((*B)[i], 0);
          *bdeg_ptr = grad;
        }
        strtok(input_line, " \t");
        field = strtok(NULL, " \t");
        if (string2mpz((*B)[grad], field, 10) != 0)
          complain("Cannot understand number %s\n", field);
        break;
      case 'M':
        strtok(input_line, " \t");
        field = strtok(NULL, " \t");
        if (string2mpz(m, field, 10) != 0)
          complain("Cannot understand number %s\n", field);
        have_m = 1;
        break;
    }
  }
  if (*adeg_ptr == -1) {
    *adeg_ptr = 1;
    *A = xmalloc(2 * sizeof(**A));
    mpz_init_set_ui((*A)[1], 1);
    mpz_init((*A)[0]);
    mpz_neg((*A)[0], m);
  }
  if (*bdeg_ptr == -1) {
    *bdeg_ptr = 1;
    *B = xmalloc(2 * sizeof(**B));
    mpz_init_set_ui((*B)[1], 1);
    mpz_init((*B)[0]);
    mpz_neg((*B)[0], m);
  }
  if (*adeg_ptr == 0 || *bdeg_ptr == 0)
    complain("Polynomials of degree zero are not allowed\n");

  {
    mpz_t x;
    i32_t i;

    if (mpz_sgn(*(*A + *adeg_ptr)) == 0) {
      complain("Leading coefficient (degree %u) vanishes\n", *adeg_ptr);
    }
    if (mpz_sgn(*(*B + *bdeg_ptr)) == 0) {
      complain("Leading coefficient (degree %u) vanishes\n", *bdeg_ptr);
    }
    for (i = 1, mpz_init_set(x, (*A)[*adeg_ptr]); i <= *adeg_ptr; i++) {
      mpz_mul(x, x, m);
      mpz_add(x, x, (*A)[*adeg_ptr - i]);
    }
    mpz_fdiv_r(x, x, N);
    if (mpz_sgn(x) != 0) {
      mpz_out_str(stderr, 10, m);
      complain(" not a root of first poly\n");
    }
    for (i = 1, mpz_set(x, (*B)[*bdeg_ptr]); i <= *bdeg_ptr; i++) {
      mpz_mul(x, x, m);
      mpz_add(x, x, (*B)[*bdeg_ptr - i]);
    }
    mpz_fdiv_r(x, x, N);
    if (mpz_sgn(x) != 0) {
      mpz_out_str(stderr, 10, m);
      complain(" not a root of second poly\n");
    }
    mpz_clear(x);
  }

  free(input_line);
}

#else
void
input_poly(mpz_t N,mpz_t **A,i32_t *adeg,mpz_t **B,i32_t *bdeg,mpz_t m,
	FILE *fp)
{ char  token[256], value[512], thisLine[1024];
  int   i, fields, have_n=0, have_m=0, have_a=0, have_b=0;
  unsigned line=0;
  mpz_t tmpA, tmpB;

  *adeg = *bdeg = 0;                                                    
  *A = xmalloc(9*sizeof(**A)); /* plenty o' room. */
  *B = xmalloc(9*sizeof(**B));
  for (i=0; i<9; i++) {
    mpz_init_set_ui((*A)[i], 0);
    mpz_init_set_ui((*B)[i], 0);
  }
  while (fgets(thisLine, sizeof(thisLine), fp) != NULL) {
    line++;
    if (strchr(thisLine, '\n') == NULL && !feof(fp))
      complain("Polynomial input line %u is too long\n", line);
    fields=sscanf(thisLine, "%255s %511s", token, value);
    if (fields < 1 || token[0] == '#') continue;
    if (strcmp(token, "END_POLY") == 0) break;
    if (fields != 2)
      complain("Missing value on polynomial input line %u\n", line);
    if (strcmp(token, "n:") == 0) {
      if (mpz_set_str(N, value, 10) != 0)
        complain("Invalid modulus on polynomial input line %u\n", line);
      have_n=1;
    } else if (strcmp(token, "m:") == 0) {
      if (mpz_set_str(m, value, 10) != 0)
        complain("Invalid root on polynomial input line %u\n", line);
      have_m=1;
    } else if ((token[0]=='c') && (token[1] >= '0') &&
               (token[1] <= '8') && token[2]==':' && token[3]=='\0') {
      if (mpz_set_str((*A)[token[1]-'0'], value, 10) != 0)
        complain("Invalid coefficient on polynomial input line %u\n", line);
      *adeg = MAX(*adeg, token[1]-'0');
      have_a=1;
    } else if ((token[0]=='Y') && (token[1] >= '0') &&
               (token[1] <= '8') && token[2]==':' && token[3]=='\0') {
      if (mpz_set_str((*B)[token[1]-'0'], value, 10) != 0)
        complain("Invalid coefficient on polynomial input line %u\n", line);
      *bdeg = MAX(*bdeg, token[1]-'0');
      have_b=1;
    }
  }
  if (ferror(fp)) complain("Error reading polynomial input\n");
  if (!have_n || mpz_sgn(N) <= 0) complain("Missing or invalid modulus n\n");
  if (!have_a || *adeg == 0 || mpz_sgn((*A)[*adeg]) == 0)
    complain("Missing or invalid first polynomial\n");

  if (!have_m) {
    if (have_b && *bdeg == 1 && mpz_invert(m, (*B)[1], N) != 0) {
      mpz_mul(m, m, (*B)[0]);
      mpz_neg(m, m);
      mpz_mod(m, m, N);
    } else {
      complain("Could not recover m from the second polynomial\n");
    }
  } else if (!have_b) {
    mpz_set_ui((*B)[1], 1);
    mpz_neg((*B)[0], m);
    *bdeg=1;
  }
  if (*bdeg == 0 || mpz_sgn((*B)[*bdeg]) == 0)
    complain("Missing or invalid second polynomial\n");

  /* Verify the polynomials: */
  mpz_init_set(tmpA, (*A)[*adeg]);
  for (i=*adeg-1; i>=0; i--) {
    mpz_mul(tmpA, tmpA, m);
    mpz_add(tmpA, tmpA, (*A)[i]);
    mpz_mod(tmpA, tmpA, N);
  }
  mpz_init_set(tmpB, (*B)[*bdeg]);
  for (i=*bdeg-1; i>=0; i--) {
    mpz_mul(tmpB, tmpB, m);
    mpz_add(tmpB, tmpB, (*B)[i]);
    mpz_mod(tmpB, tmpB, N);
  }
  if (mpz_sgn(tmpA) || mpz_sgn(tmpB))
    complain("m is not a common root of the NFS polynomials\n");
  mpz_clear(tmpB); mpz_clear(tmpA);
}
#endif
