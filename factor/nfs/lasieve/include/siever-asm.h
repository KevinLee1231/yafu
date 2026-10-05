#ifndef YAFU_SIEVER_ASM_H
#define YAFU_SIEVER_ASM_H

/* 汇编与 C++ 之间仅存的接口。
 *
 * factor/nfs/lasieve/kernels 下有的低层源，它们提供的符号是裸名字；而
 * 调用方（ecm.cpp、pm1.cpp、mpqs.cpp、mpqs3.cpp）现在是 C++，不加声明的话
 * 调用点会被改写成带修饰的名字，链接期找不到。这一层是真实的 C/汇编 ABI
 * 边界，extern "C" 是长期需要的。
 *
 * 别的筛法器接口（lasieve_setup、mpqs_factor、mpqs3_factor、lasieve_run、
 * mpz_trialdiv）都是 C++ 定义的，声明在各自的头里，用的是普通 C++ 链接，
 * 不该出现在这里。
 *
 * 这两个符号每个 I 值各有一份，放在各自的命名空间（lasieve_I<N>）里，
 * 头里的声明也跟着进同一个命名空间，对得上。
 */

/* u32_t / ulong / mpz_t 来自 siever-config.h（它已经引了 gmp.h，不会重复），
 * size_t 来自 stddef.h。这两个必须落在 extern "C" 之外：gmp.h 尾部用
 * std::ostream 声明 operator<<，套进 extern "C" 会报
 * conflicting declaration of C function。 */
#include "lasieve_ns.h"
#include "siever-config.h"
#include <stddef.h>


namespace lasieve_ns {
void gcd(ulong *a, ulong *b, ulong *c);
int asm_invert(ulong *m, ulong *n);
}  /* namespace lasieve_ns */


/* 汇编提供：GMP 试除。gnfs-lasieve4e.cpp 里也有一份同名实现，
 * 但那段在 #ifndef ASM_MPZ_TD 里，本构建定义了它，所以不生效。 */
namespace lasieve_ns {
u32_t *mpz_trialdiv(mpz_t N, u32_t *pbuf, u32_t ncp, char *errmsg);
}  /* namespace lasieve_ns */



#endif /* YAFU_SIEVER_ASM_H */
