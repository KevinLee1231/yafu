/* mpz-td —— 由 mpz-td.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * GMP 风格的多精度 Montgomery 试除：把 nlimbs 长的数组逐 limb 乘上
 * modular_inverse（= prime 的模 2^64 逆元），再乘 prime，借位传给下一段。
 * 返回值是最后一段借出去的高位（也就是商的高 limb）。
 *
 * ============================== 最关键的一条 ==============================
 * x86-64 的 `mulq r/m64` 隐含乘法源是 **rax**，不是 rdx:rax；rdx 只是目的
 * 寄存器的高半（Intel 的记法：RDX:RAX := RAX * r/m64），乘法前 rdx 里是什么
 * 根本不参与运算。
 *
 * 因此循环里连续两条 mulq 是「低位接力」：
 *     mulq %rsi          ; rdx:rax = 当前 limb * inv
 *     movq %rax,(%r10)   ; 把**低半**写回去
 *     mulq %rdi          ; rdx:rax = 低半 * prime     ← 用的是低半，不是 rdx:rax
 * 所以第二条乘法的输入是第一条乘积的**低 64 位**，高半在这一步被丢掉。
 * C++ 里对应的是
 *     u64 lo = (u64)((__uint128_t)a * inv);   // 存回去
 *     u64 hi = (u64)(((__uint128_t)lo * prime) >> 64);
 * 如果写成 `u = (__uint128_t)t * prime`（拿整个 128 位积去乘）就错了：
 * 那是把 (inv·a)·prime 的高半也用上了。实测过：prime=2、inv=2^64-1、
 * nlimbs=1、a0=2^64-1 时汇编返回 0，而「高半也参与」的写法返回
 * 2^64-4，差得很远。差分测试里 prime=2、inv=2^64-1 这一组正是把这条钉住的。
 *
 * 顺带说明交接材料里的那个疑点：入口到第一条 mul 之前 rdx 确实是第三个入参
 * （limb_ptr）的值，看起来像「寄存器残留」。实测不是 —— 那几条指令只是
 * 读 rdx（movq 复制、leaq、movq (%rdx),%rax），而 mulq 根本不看 rdx 的
 * 输入。验证办法：gdb 单步看到第一条 mul 前 rdx = 指针值、之后 rdx = 0；
 * 再用一段独立手写汇编对「rdx=1 与 rdx=0」两种输入做对照（mulq 的高位
 * 两者相同），WSL 里和 Windows 原生 gcc 各做一遍，结论一致。
 * 所以两侧不需要「拿同一个指针」，差分测试用两个不同数组也成立。
 *
 * ============================== 指针推进顺序 ==============================
 * 汇编是「先 mulq prime，再把指针前进 8 字节，然后读 *limb_ptr」——
 * 也就是减法用的是**下一段**的值，而当前段早在 mulq inv 之后就写掉了。
 * 循环条件是 `ja`（严格大于）：p > 最后一个 limb 才继续。
 *
 * nlimbs 的契约：>= 1。nlimbs = 0 时汇编照样读改写 limb_ptr[0]（跑到数组
 * 外面去了），但 nlimbs-1 那句 leaq 会回绕，两侧行为仍然一致；差分测试里
 * 数组留了哨兵，两侧读到的内容相同，所以 nlimbs=0 也纳入了比对。
 *
 * 借位那对标志位：
 *     subq %rdx,%rax     ; rax -= (high + carry)，CF = 借位
 *     adcq %r9,%r9       ; carry = carry + carry + CF
 * 前面刚 xorq %r9,%r9 把 carry 清成 0，所以 adcq 等于「把 CF 收进 carry」，
 * 翻译成 C++ 就是无符号比较 a < hi、借位则 carry = 1。
 */

#include <stdint.h>

#include "siever-config.h"
#include "mpqs-config.h"

typedef u64_t u64;

/* 声明在 asm/mpz-trialdiv.c 里，带的是 mpz-trialdiv 那边手写的
 * `mp_limb_t ASM_ATTR mpz_asm_td(...)`，也就是 extern "C"。mpqs-config.h
 * 里没有这一条，所以这里必须自己标 C 链接，否则真实链接时
 * mpz-trialdiv.cpp 的调用会找 C 符号、这边给的是 C++ 修饰名。
 * 符号名本身是裸的（汇编提供的），不加 extern "C" 编译器会改成
 * _Z10mpz_asm_tdmmmPm，链接期找不到。 */
extern "C" u64 mpz_asm_td(u64 prime, u64 modular_inverse, u64 *limb_ptr,
                          u64 nlimbs)
{
	u64 *p = limb_ptr;
	/* 汇编里 limb_ptr_ub = 入参 + nlimbs*8 - 8，也就是最后一个 limb。 */
	u64 *const ub = limb_ptr + (nlimbs ? nlimbs - 1 : 0);
	u64 carry = 0;
	u64 a;
	u64 lo, hi;

	a = *p;                             /* movq (%rdx),%rax */
	if (nlimbs < 2)                     /* cmpq $2,%rcx ; jb */
		goto last_limb;

	for (;;) {
		lo = (u64)((__uint128_t)a * modular_inverse);   /* mulq %rsi */
		*p = lo;                                       /* movq %rax,(%r10) */
		/* mulq %rdi：隐含源是 rax，也就是上面刚写回去的低半 */
		hi = (u64)(((__uint128_t)lo * prime) >> 64);
		++p;                                            /* leaq 8(%r10),%r10 */
		a = *p;                                         /* movq (%r10),%rax */
		hi += carry;                        /* addq %r9,%rdx */
		carry = 0;                          /* xorq %r9,%r9 */
		if (a < hi)                         /* subq %rdx,%rax 的借位 */
			carry = 1;                      /* adcq %r9,%r9 */
		a -= hi;
		/* cmpq %r10,%r8 ; ja —— AT&T 里目标是最后一个操作数，所以这条比的是
		 * **ub - p**，ja（严格大于）成立即 ub > p。也就是说循环条件是
		 * 「ub 还比当前指针大」，nlimbs 段要跑 nlimbs-1 次，最后一段留给
		 * last_limb。写成 p > ub 会把两个操作数记反，循环次数和收尾段全错。 */
		if (ub > p)
			continue;
		break;
	}

last_limb:
	lo = (u64)((__uint128_t)a * modular_inverse);       /* mulq %rsi */
	*p = lo;                                            /* movq %rax,(%r10) */
	hi = (u64)(((__uint128_t)lo * prime) >> 64);        /* mulq %rdi */
	hi += carry;                                        /* addq %r9,%rdx */
	return hi;                                          /* movq %rdx,%rax */
}