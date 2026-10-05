#ifndef YAFU_SIEVER_ASM_H
#define YAFU_SIEVER_ASM_H

/* u32_t / ulong / mpz_t 都来自 siever-config.h（C 侧已经引过 gmp.h，不会重复），
 * size_t 来自 stddef.h。这两个必须落在 extern "C" 之外：gmp.h 尾部用
 * std::ostream 声明 operator<<，套进 extern "C" 会报
 * conflicting declaration of C function。 */
#include "siever-config.h"
#include <stddef.h>

/* 汇编与 C++ 之间的接口。
 *
 * 这些函数有一半在 factor/nfs/lasieve/asm 的汇编里实现，另一半在筛法器的
 * C++ 文件里实现，但调用方在 C++ 里。声明必须放在头里并带 C 链接，
 * 否则 C++ 会把调用点改写成带修饰的名字，而定义那一侧是汇编/裸名字，
 * 链接期对不上。
 *
 * gcd 和 asm_invert 由汇编提供，并且按 I 值改名（子 Makefile 用
 * -Dname=nameI%d 编译六份），所以这个头只应该被按 I 编译的源包含。
 */

#ifdef __cplusplus
extern "C" {  /* yafu-cpp-linkage */
#endif

/* 汇编提供：64 位 gcd 与模逆 */
void gcd(ulong *a, ulong *b, ulong *c);
int asm_invert(ulong *m, ulong *n);

/* C++ 提供、跨编译单元调用 */
u32_t *mpz_trialdiv(mpz_t N, u32_t *pbuf, u32_t ncp, char *errmsg);
int mpqs_factor(mpz_t N, size_t max_bits, mpz_t **factors);
int mpqs3_factor(mpz_t N, size_t max_bits, mpz_t **factors);
int lasieve_run(int I, int argc, char **argv);

#ifdef __cplusplus
}  /* yafu-cpp-linkage */
#endif

#endif /* YAFU_SIEVER_ASM_H */
