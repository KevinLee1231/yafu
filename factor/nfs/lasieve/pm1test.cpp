/*
Copyright (C) 2001 Jens Franke, T. Kleinjung.
This file is part of gnfs4linux, distributed under the terms of the
GNU General Public Licence and WITHOUT ANY WARRANTY.

You should have received a copy of the GNU General Public License along
with this program; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
*/


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gmp.h>
#include "siever-config.h"
#include "if.h"
#include "pm1.h"
#include "zeit.h"
#include "montgomery_mul.h"
#include "lasieve_ctx.h"

/* 单份对象链的是 lasieve_single 这一份（lasieve_ns.h 的默认值），
 * 而 per-I 的名字现在都在命名空间里，所以这里要把它引进来。 */
using namespace lasieve_ns;



int main(int argc, char *argv[])
{
  lasieve_current_ctx = lasieve_ctx_new();
  if (lasieve_current_ctx == NULL)
    complain("out of memory\n");
  mpz_t N;
  u32_t n, i, npm1, B1, B2;
  FILE *fi;
  mpz_t *f;
  char *input_line=NULL;
  size_t input_line_alloc=0;
  int nc;

  setbuf(stdout,NULL);
  if (argc<3) complain("Usage: pm1test B1 filename [ B2 ]\n");
  B1=(u32_t)atoi(argv[1]);
  initzeit(13); zeita(0);
  mpz_init(N); n=0; npm1=0;
  fi=fopen(argv[2],"r");
  if (fi==NULL) complain("cannot open input file %s\n",argv[2]);
  if (argc>3) B2=strtoul(argv[3],NULL,10); else B2=10*B1;
  while (getline(&input_line,&input_line_alloc,fi)>0) {
    if (mpz_set_str(N,input_line,10)) break;
    nc=pm1_factor(N,B1,B2,&f); npm1++;
    if (nc>0) { n++; /*gmp_printf("%Zd  ",*f);*/ }
    printf(".");
  }
  fclose(fi);
  if (n) {
    printf("P-1-tests: %u  Factors: %u\n",npm1,n);
  } else printf("No factor found!\n");
  printf("\n");
  zeitb(0);
  for (i=0; i<13; i++) printzeit(i); printf("\n");
  exit(0);
}
