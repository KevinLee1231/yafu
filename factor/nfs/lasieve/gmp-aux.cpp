/*2:*/

#include <stdlib.h> 
#include <sys/types.h> 
#include <string.h> 
#include <gmp.h> 
#include "siever-config.h"
#include "if.h"
#include "gmp-aux.h"

/* 单份对象：lasieve_ns 解析成 lasieve_single，下面用它链进来 complain。*/
using namespace lasieve_ns;














/*:2*//*3:*/

int
string2mpz(mpz_t rop,char*x,int base)
{
size_t l;
char*y;
int rv;

x+= strspn(x," \t+");
l= strlen(x);
while(l> 0&&(x[l-1]=='\n'||x[l-1]=='\r'))l--;
if(l==0){
mpz_set_ui(rop,0);
return -1;
}
if((y= (char*)malloc(l+1))==NULL)
  complain("string2mpz: out of memory\n");
memcpy(y,x,l);
y[l]= '\0';
rv= mpz_set_str(rop,y,base);
free(y);
return rv;
}

/*:3*//*4:*/

#ifdef NEED_MPZ_MUL_SI
void mpz_mul_si(mpz_t x,mpz_t y,long int z)
{
if(z<0){
mpz_mul_ui(x,y,(unsigned long)(-z));
mpz_neg(x,x);
}else{
mpz_mul_ui(x,y,(unsigned long)z);
}
}
#endif/*:4*/
