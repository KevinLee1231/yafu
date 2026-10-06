/* asm_64bit —— 由 asm_64bit.asm 翻译而来
 * (Copyright (C) 2006 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 两个 64 位模运算，都读全局 modulo64。
 *
 * asm_modadd64 里唯一「读标志位」的地方是那一条 cmovc。汇编原文：
 *     movq modulo64(%rip),%rdx
 *     movq %rdi,%rax
 *     subq %rdx,%rax          ; rax = a - modulo64
 *     addq %rsi,%rax          ; rax = a - modulo64 + b
 *     movq $0,%rcx
 *     cmovcq %rcx,%rdx        ; 借位则 rdx = 0，否则 rdx = modulo64
 *     addq %rdx,%rax
 *
 * 关键：cmovc 判的是 **add 的进位**，不是 sub 的借位 —— add 会覆盖 sub 留下
 * 的 CF。所以条件是「a - modulo64 + b 溢出 64 位」，也就是无符号意义下的
 * a + b < modulo64。按 sub 的借位写成 (a < modulo64) ? 0 : modulo64 方向正好
 * 反了，差分测试在 m=1000 那种小模数上立刻炸出来。
 *
 * asm_modmul64 是 mulq 出 128 位乘积再 divq 取余数。C++ 里必须用
 * __uint128_t：直接写 (a*b) 会在 64 位上就回绕掉，余数整个错 —— 差分测试
 * 在 m=2^64-1、a=b=2^64-2 这一组上抓到了。
 *
 * 契约（两边都一样，差分测试必须落在契约内）：
 *   - modulo64 非 0，否则 divq 除零直接 #DE
 *   - a、b 都在 [0, modulo64) 内，否则 divq 商溢出（乘积高 64 位 >= 模数）
 *     一样是 #DE。越界时汇编崩、C++ 不崩，两边行为不可比。
 * 真实调用点都传模内的值（这两个是 Montgomery 运算的一半），所以不修。
 */

#include <stdint.h>

#include "siever-config.h"
#include "64bit.h"

typedef u64_t u64;

u64 asm_modadd64(u64 a, u64 b)
{
	u64 r = a - modulo64;
	u64 s = r + b;
	/* s < r 等价于 r + b 溢出，即 add 置的那一位 CF */
	u64 d = (s < r) ? 0 : modulo64;
	return s + d;
}

u64 asm_modmul64(u64 a, u64 b)
{
	return (u64)((__uint128_t)a * (__uint128_t)b % (__uint128_t)modulo64);
}