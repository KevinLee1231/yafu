
#include "lasieve_ns.h"


/*
  Copyright (C) 2004 Jens Franke, Torsten Kleinjung
  This file is part of mpqs4linux, distributed under the terms of the 
  GNU General Public Licence and WITHOUT ANY WARRANTY.

  You should have received a copy of the GNU General Public License along
  with this program; see the file COPYING.  If not, write to the Free
  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

/* modulo32 is addressed by name from the inline asm below, and inline asm sees
 * assembler symbols, not C++ entities: a variable inside namespace lasieve_ns
 * gets a compiler-mangled name whose exact spelling is ABI-specific.  So the
 * variable carries an explicit assembler label instead.  LASIEVE_STR(lasieve_ns)
 * spells lasieve_I<N> under -Dlasieve_ns=lasieve_I<N> and lasieve_single
 * otherwise, so the label is per-I without the command line having to name it.
 *
 * This replaces the old -DMODULO32_ASM_NAME='"modulo32I<N>"'.  An explicit asm
 * label is emitted verbatim, so the Darwin leading-underscore case the old
 * block had to special-case no longer applies. */
#define LASIEVE_STR2(x) #x
#define LASIEVE_STR(x) LASIEVE_STR2(x)
#define MODULO32_ASM LASIEVE_STR(lasieve_ns) "_modulo32"

namespace lasieve_ns {
volatile extern u32_t modulo32 __asm__(MODULO32_ASM);
u32_t gcd32(u32_t x, u32_t y);
}  /* namespace lasieve_ns */

int jac32(u32_t x,u32_t y);
u32_t modpow32(u32_t x,u32_t a);
u32_t modsqrt32(u32_t x);
namespace lasieve_ns {
u32_t ASM_ATTR asm_modinv32(u32_t x);
}  /* namespace lasieve_ns */


#define modinv32(x) asm_modinv32(x)


static inline u32_t modsq32(u32_t x)
{
  u32_t res,clobber;
  __asm__ volatile ("mull %%eax\n"
	   "divl " MODULO32_ASM "(%%rip)" : "=d" (res), "=a" (clobber) : "a" (x) : "cc" );
  return res;
}

static inline u32_t modmul32(u32_t x,u32_t y)
{
  u32_t res,clobber;
  __asm__ volatile ("mull %%ecx\n"
	   "divl " MODULO32_ASM "(%%rip)" : "=d" (res), "=a" (clobber) : "a" (x), "c" (y) :
	   "cc");
  return res;
}

static inline u32_t modadd32(u32_t x,u32_t y)
{
  u32_t res;
#ifdef HAVE_CMOV
  __asm__ volatile ("xorl %%edx,%%edx\n"
	   "addl %%eax,%%ecx\n"
	   "cmovc " MODULO32_ASM "(%%rip),%%edx\n"
	   "cmpl " MODULO32_ASM "(%%rip),%%ecx\n"
	   "cmovae " MODULO32_ASM "(%%rip),%%edx\n"
	   "subl %%edx,%%ecx\n"
	   "2:\n" : "=c" (res) : "a" (x), "c" (y) : "%edx", "cc");
#else
  __asm__ volatile ("addl %%eax,%%ecx\n"
	   "jc 1f\n"
	   "cmpl " MODULO32_ASM "(%%rip),%%ecx\n"
	   "jb 2f\n"
	   "1:\n"
	   "subl " MODULO32_ASM "(%%rip),%%ecx\n"
	   "2:\n" : "=c" (res) : "a" (x), "c" (y) : "cc");
#endif
  return res;
}

static inline u32_t modsub32(u32_t subtrahend,u32_t minuend)
{
  u32_t res;
#ifdef HAVE_CMOV
  __asm__ volatile ("xorl %%edx,%%edx\n"
	   "subl %%eax,%%ecx\n"
	   "cmovbl " MODULO32_ASM "(%%rip),%%edx\n"
	   "addl %%edx,%%ecx\n"
	   "1:" : "=c" (res) : "a" (minuend), "c" (subtrahend) : "%edx", "cc");
#else
  __asm__ volatile ("subl %%eax,%%ecx\n"
	   "jae 1f\n"
	   "addl " MODULO32_ASM "(%%rip),%%ecx\n"
	   "1:" : "=c" (res) : "a" (minuend), "c" (subtrahend) : "cc" );
#endif
  return res;
}

#undef MODULO32_ASM

