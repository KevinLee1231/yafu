/* (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * Montgomery 形式的 Miller-Rabin 素性测试，模数是 64 位奇数。
 * 调用点在 kernels/psp.c：多精度数只有一个 limb 时走 pt64(n[0]._mp_d[0])。
 *
 * ============================ 全篇最重要的一条 ============================
 * x86-64 的 `mulq r/m64` 的隐含乘法源是 **rax**，不是 rdx:rax；rdx 只是
 * 目的寄存器的高半。Intel 手册的记法就是 `RDX:RAX := RAX * r/m64`，
 * 乘法前 rdx 里是什么完全不参与运算。
 *
 * 这条如果记错，后面全盘皆错，而且错得很隐蔽：
 *   - modsq64 里连续三条 mulq 是一条「低位接力」的链：t*t 的低半继续乘
 *     mm64_aux，低半再继续乘 N。rdx 看着像乘法源，其实只是上一条的结果，
 *     被当低半用。
 *   - 交接材料曾经怀疑 modsq64 / mpz_asm_td 的入口没清 rdx，于是把 rdx 当成
 *     「上一个入参的残留」照抄进 C++，那是错的。实测（gdb 单步 + 独立
 *     手写汇编 + Windows 原生 gcc 各做一遍）：mulq 的 rdx 输入在 rdx!=0 时
 *     也完全不影响结果，rdx:rax = rax * r/m64。C++ 里的 (__uint128_t)a * b
 *     就是对的，不需要任何「寄存器残留」的注释。
 * =======================================================================
 *
 * 契约（越界输入汇编会自己出事，不构成用例）：
 *   N 必须是 >= 2 的奇数。
 *   - N = 0：divq 除零；N = 1：divq 的商 2^64 装不下 64 位，#DE。
 *   - N 是偶数（含 2）：N-1 是奇数，v2(N-1) = 0，于是 b = 0；后面
 *     `decq b; jz pt64_comp` 从 0 减到 2^64-1，永远减不到 0，等于死循环。
 *   真实调用点 psp() 只在 mpz_size(n) < 2 且非 0 时进来；偶数在
 *   psp2/set_montgomery_multiplication 那层已经被判掉。
 *
 * 另一个读法上的坑：`movq $1,%rdx; xorq %rax,%rax; divq %rdi` 算的是
 * rdx:rax = 2^64 除以 N，所以 rdx 拿到的是 **2^64 mod N**，不是 1 % N。
 * 这个值后来被当成 Montgomery 意义下的 1（one = R mod N）用，别写成 1。
 *
 * modsq64 是 Montgomery 平方，X 是 N 的模逆（N^{-1} mod 2^64，注意是
 * **不带负号**的逆元，因为汇编末尾用的是减法：t - high(m*N) 而不是加法）。
 * Montgomery 形式下的 -1 是 N - R mod N，也就是汇编里 subq %rsi,%rcx 那句
 * 算出来的 N - one，不是 N - 1。照抄，别写成 N-1。
 */

#include <stdint.h>

#include "siever-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u64_t u64;

/* 声明在 kernels/psp.c 里：`u64_t ASM_ATTR pt64(u64_t);`。mpqs-config.h 里没有
 * 这一条，所以这里自己写 ；符号名本身是裸的（汇编提供），
 * 少了 链接期会找不到。 */
u64 pt64(u64 N);

/* mpqs_256_inv_table 定义在 mpqs.cpp（kernels/invtab.c 里还有一份同样的，
 * liblasieve.a 链的是后者），table[i] = (2i+1) 的模 2^8 逆元，
 * 汇编用 movzbq 取，所以这里必须是 unsigned char 而不是 uchar 数组别名。 */
extern unsigned char mpqs_256_inv_table[128];

/* modsq64：Montgomery 平方，X = N^{-1} mod 2^64。
 *
 * 逐条对应：
 *     movq %r11,%rax ; mulq %r11   →  rdx:rax = t*t
 *     movq %rdx,%r11               →  t 变成 t*t 的高半
 *     mulq %r8                     →  隐含源是 rax（t*t 的**低半**），不是 r11
 *     mulq %rdi                    →  隐含源还是 rax（上一条的低半）
 *     subq %rdx,%r11 ; cmovbq ; addq →  t - high(m*N)，借位就补一个 N
 *
 * 借位判断必须写成无符号比较：subq 设 CF 的条件正是 h < hi。
 * 补 N 与否互斥（cmov 只会置成 N），所以两种写法结果一样，但用无符号比较
 * 表达「借位」和汇编一一对应，改成有符号比较会在 h、hi 相差很大的用例上错。
 */
static inline u64 modsq64(u64 t, u64 N, u64 X)
{
	__uint128_t t2 = (__uint128_t)t * t;
	u64 h = (u64)(t2 >> 64);                        /* movq %rdx,%r11 */
	u64 m = (u64)t2 * X;                             /* mulq %r8，用 rax */
	u64 hi = (u64)(((__uint128_t)m * N) >> 64);      /* mulq %rdi，用 rax */
	return (h < hi) ? (h - hi) + N : h - hi;         /* sub / cmovb / add */
}

/* pt64_auxcalc：把 8 位的表值提升成 64 位的 N^{-1}。
 *
 * 每轮是 Newton 迭代 X ← X(2 - XN) = 2X - X^2 N：正确位数翻一倍，
 * 8 → 16 → 32 → 64，三轮正好 64 位。
 *
 *     movq %r8,%rax ; mulq %r8 → rdx:rax = X*X
 *     shlq $1,%r8              → X 变成 2X
 *     mulq %rdi                → rdx:rax = (X*X 的低半) * N
 *     subq %rax,%r8            → 2X 减去它的低 64 位
 *
 * 这里 sub 用的是上一步乘积的**低半**，所以 (X*X)*N 的高半被丢掉是原样语义，
 * 不是笔误：Newton 公式里本来就只需要 mod 2^64 的那一项。
 */
static u64 mont_inv64(u64 N, u64 X)
{
	int i;

	for (i = 0; i < 3; i++) {
		__uint128_t s = (__uint128_t)X * X;
		u64 lo = (u64)(((__uint128_t)(u64)s * N));
		X = (X << 1) - lo;
	}
	return X;
}

/* pt64_modpow_dupt：t ← 2t mod N。
 *
 *     shlq $1,%r11     → t <<= 1，CF = 被移出去的那一位
 *     cmovcq %rdi,%rax → CF=1（t 原本 >= 2^63）就取 N
 *     cmpq %rdi,%r11 ; cmovaeq → t > N（无符号）也取 N
 *     subq %rax,%r11
 *
 * 两个 cmov 都只是「把 rax 置成 N」，所以合成一个条件即可；条件是「或」，
 * 不能写成「否则」（移出进位和 t>N 可能同时成立，结果仍是减一个 N）。
 * cmpq 给的是 t - N，ja 是无符号大于，所以 t == N 时不减。
 */
static inline u64 dupt(u64 t, u64 N)
{
	u64 r = t << 1;

	if ((t >> 63) || r > N)
		r -= N;
	return r;
}

u64 pt64(u64 N)
{
	u64 one, X, auxreg, expon, b, t;

	/* movq $1,%rdx ; xorq %rax,%rax ; divq %rdi
	 * 被除数是 rdx:rax = 2^64，所以 one = 2^64 mod N = R mod N，
	 * 它是 Montgomery 形式下的 1。N <= 1 时这里 #DE（见文件头契约）。 */
	one = (u64)(((__uint128_t)1 << 64) % N);

	/* andq $255,%rcx ; shrq $1,%rcx ; movzbq table(%r8,%rcx,1),%r8 */
	X = mpqs_256_inv_table[(N & 255) >> 1];
	X = mont_inv64(N, X);

	/* bsfq %r9,%rcx ; shrq %cl,%r9 ; movq %rcx,%r10
	 * b = v2(N-1)，expon = (N-1) 的奇数部分。expon 恒非 0（N >= 2），
	 * 所以 bsf 的「零输入未定义」那一条碰不到；写成循环和 bsf 一样。 */
	expon = N - 1;
	b = 0;
	{
		u64 e = expon;

		while ((e & 1) == 0) {
			e >>= 1;
			b++;
		}
		expon = e;
	}

	/* bsrq %r9,%rcx ; movq $1,%rax ; shlq %cl,%rax ; movq %rax,%rcx
	 * auxreg = 1 << bsr(expon)，之后每轮右移一位当滑动窗口掩码。 */
	auxreg = (u64)1 << (63 - __builtin_clzll(expon));

	/* t = one，然后无条件翻一倍（对应指数最高位的那个 1）。 */
	t = dupt(one, N);

	for (;;) {
		auxreg >>= 1;                    /* shrq $1,%rcx ; jnz 下一轮 */
		if (auxreg == 0)
			break;
		t = modsq64(t, N, X);            /* call modsq64 */
		if (auxreg & expon)               /* testq %rcx,%r9 */
			t = dupt(t, N);
	}

	/* cmpq %r11,%rsi → t == one 即 Montgomery 意义下的 1 */
	if (t == one)
		return 1;

	/* movq %rdi,%rcx ; subq %rsi,%rcx → N - one，即 Montgomery 意义下的 -1。
	 * 注意是 N - one 不是 N - 1；one = 2^64 mod N 时两者常常不同（N=7 时
	 * one=2，比较目标是 5 而不是 6）。 */
	auxreg = N - one;

	for (;;) {
		if (t == auxreg)                  /* cmpq %r11,%rcx ; je pt64_prime */
			return 1;
		if (--b == 0)                     /* decq %r10 ; jz pt64_comp */
			return 0;
		t = modsq64(t, N, X);            /* call modsq64 */
	}
}
}  /* namespace lasieve_ns */
