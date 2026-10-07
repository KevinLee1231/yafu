#ifndef _MONTGOMERY_MUL_H_
#define _MONTGOMERY_MUL_H_

#define NMAX_ULONGS   8
// SMJS #define ulong  unsigned long

// SMJS For ulong type
#include "lasieve_ns.h"
#include "siever-config.h"


/* 这几个 montgomery 暂存区在 C 时代靠 -fcommon 在 ecm 与 pm1
 * 之间合成一份；C++ 里每个定义都是强的，所以改成头里声明、
 * 只留一份定义（在 ecm.cpp）。*/
namespace lasieve_ns {
extern ulong mm_A[NMAX_ULONGS];
extern ulong mm_B[NMAX_ULONGS];
extern ulong mm_C[NMAX_ULONGS];
extern ulong mm_a[NMAX_ULONGS];
extern ulong mm_b[NMAX_ULONGS];
extern ulong mm_one[NMAX_ULONGS];
extern ulong mm_prod[NMAX_ULONGS];
extern ulong mm_u[NMAX_ULONGS];
extern ulong mm_v[NMAX_ULONGS];
extern ulong mm_w[NMAX_ULONGS];
extern ulong mm_x[NMAX_ULONGS];
extern ulong mm_x1[NMAX_ULONGS];
extern ulong mm_z[NMAX_ULONGS];
extern ulong mm_z1[NMAX_ULONGS];
}  /* namespace lasieve_ns */


namespace lasieve_ns {
extern void ASM_ATTR (*asm_mulmod)(ulong *,ulong *,ulong *);
extern void ASM_ATTR (*asm_zero)(ulong *);
extern void ASM_ATTR (*asm_copy)(ulong *,ulong *);
extern void ASM_ATTR (*asm_half)(ulong *);
extern void ASM_ATTR (*asm_sub)(ulong *,ulong *,ulong *);
extern void ASM_ATTR (*asm_add2)(ulong *,ulong *);

extern void (*asm_sub_n)(ulong *,ulong *);
extern void (*asm_squmod)(ulong *,ulong *);
extern void (*asm_diff)(ulong *,ulong *,ulong *);
extern void (*asm_add2_ui)(ulong *,ulong);
extern int (*asm_inv)(ulong *,ulong *);

void init_montgomery_multiplication();
int set_montgomery_multiplication(mpz_t);
}  /* namespace lasieve_ns */


// SMJS Moved protos to here from c file so can be used in noasm64.c as well
// SMJS Removed externs
namespace lasieve_ns {
void ASM_ATTR asm_mulm64(ulong *,ulong *,ulong *);
void ASM_ATTR asm_zero64(ulong *);
void ASM_ATTR asm_sub64_3(ulong *,ulong *,ulong *);
void ASM_ATTR asm_copy64(ulong *,ulong *);
void ASM_ATTR asm_half64(ulong *);
void ASM_ATTR asm_add64(ulong *,ulong *);

void asm_sub_n64(ulong *,ulong *);
void asm_add64_ui(ulong *,ulong);
void asm_sqm64(ulong *,ulong *);
void asm_diff64(ulong *,ulong *,ulong *);
int asm_inv64(ulong *,ulong *);


void ASM_ATTR asm_sub128_3(ulong *,ulong *,ulong *);
void ASM_ATTR asm_zero128(ulong *);
void ASM_ATTR asm_copy128(ulong *,ulong *);
void ASM_ATTR asm_half128(ulong *);
void ASM_ATTR asm_mulm128(ulong *,ulong *,ulong *);
void ASM_ATTR asm_add128(ulong *,ulong *);

void asm_sub_n128(ulong *,ulong *);
void asm_sqm128(ulong *,ulong *);
void asm_diff128(ulong *,ulong *,ulong *);
void asm_add128_ui(ulong *,ulong);
int asm_inv128(ulong *,ulong *);


void ASM_ATTR asm_zero192(ulong *);
void ASM_ATTR asm_mulm192(ulong *,ulong *,ulong *);
void ASM_ATTR asm_sub192_3(ulong *,ulong *,ulong *);
void ASM_ATTR asm_add192(ulong *,ulong *);
void ASM_ATTR asm_copy192(ulong *,ulong *);
void ASM_ATTR asm_half192(ulong *);

void asm_sub_n192(ulong *,ulong *);
void asm_sqm192(ulong *,ulong *);
void asm_diff192(ulong *,ulong *,ulong *);
void asm_add192_ui(ulong *,ulong);
int asm_inv192(ulong *,ulong *);
}  /* namespace lasieve_ns */


#endif
