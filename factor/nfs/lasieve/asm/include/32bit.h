/*
  Copyright (C) 2004 Jens Franke, Torsten Kleinjung
  This file is part of mpqs4linux, distributed under the terms of the 
  GNU General Public Licence and WITHOUT ANY WARRANTY.

  You should have received a copy of the GNU General Public License along
  with this program; see the file COPYING.  If not, write to the Free
  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

volatile extern u32_t modulo32;
u32_t gcd32(u32_t x, u32_t y);
int jac32(u32_t x,u32_t y);
u32_t modpow32(u32_t x,u32_t a);
u32_t modsqrt32(u32_t x);
u32_t ASM_ATTR asm_modinv32(u32_t x);

#define modinv32(x) asm_modinv32(x)

/* The C identifier modulo32 and the name the assembly sees are two different
 * things.  A per-I build gives the C symbol an I suffix (see ../../I_SYMBOLS);
 * the inline asm below would still refer to the unsuffixed one otherwise, and
 * the #undef that used to follow this macro also killed the -D, so every
 * later use stopped being renamed. */
/* MODULO32_ASM_NAME comes first on purpose: it is what a per-I build passes, and
 * the leading-underscore form is a Darwin symbol convention, not a different
 * symbol.  Testing __APPLE__ first would hand macOS the unsuffixed name and the
 * six I values would then share one montgomery state. */
#if defined(MODULO32_ASM_NAME)
#if defined(__APPLE__)
#define MODULO32_ASM "_" MODULO32_ASM_NAME
#else
#define MODULO32_ASM MODULO32_ASM_NAME
#endif
#elif defined(__APPLE__)
#define MODULO32_ASM "_modulo32"
#else
#define MODULO32_ASM "modulo32"
#endif

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
