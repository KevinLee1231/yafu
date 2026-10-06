/* tdslinie 共用的定义块 —— 由 tdslinie1/2/3.cpp 各自 #include 之前复制过来。
 *
 * 说明：本文件只是把四个内核共用的常量与类型约定集中一处，避免四份拷贝
 * 抄错。tdslinie.cpp 不 include 它（它是主文件，常量在文件内定义）。
 */

#ifndef TD_COMMON_H
#define TD_COMMON_H

#include <stdint.h>

#include "siever-config.h"

#ifndef I_bits
#error "I_bits 必须由 Makefile 的 -DI_bits=<11..16> 传入（与汇编的 -Dn_i_bits=I-1 对应）"
#endif

static_assert(L1_BITS == 15, "ls-defs.asm 的 l1_bits 固定为 15");
static_assert(I_bits >= 2 && I_bits <= 16, "I_bits 超出 per-I 库的范围");

/* ls-defs.asm: n_i = 2**n_i_bits, j_per_strip = 2**(l1_bits-n_i_bits)，
 * 而 Makefile 传的是 -Dn_i_bits = I-1。 */
#define TD_NI_BITS     (I_bits - 1)
#define TD_NI          ((u64_t)1 << TD_NI_BITS)
#define TD_J_PER_STRIP (1 << (L1_BITS - TD_NI_BITS))

/* 汇编里 sieve_ptr_ub / sieve_ptr 是 64 位寄存器，`subq`/`leaq` 会回绕，
 * 随后的 `cmpq ; ja` 按**无符号**比较。这些运算一旦写成 C++ 指针就算
 * 越界（UB），GCC 有权据此改写分支，实测会与汇编不符。
 * 所以内核内部一律用 uintptr_t 承载地址，解引用时才经 TD_AT 转成
 * unsigned char* —— 那一瞬间的转换不引入任何算术，语义与汇编一致。 */
#define TD_AT(addr) (*(const unsigned char *)(uintptr_t)(addr))

#endif /* TD_COMMON_H */
