/* asm_arith128 —— 128 位蒙哥马利模 N 算术内核的 C++ 版。
 *
 * 由 factor/nfs/lasieve/asm/asm_arith128.asm 翻译而来
 *（Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL）。
 * 原文件是 m4 模板，寄存器别名来自文件头的 define(...)：
 *
 *   rop=%rdi op=%rsi op1=%rsi op2=%rdx
 *   res0=%r11 res1=%r13 res2=%r12 res3=%r11   <-- 注意 res3 和 res0 同寄存器
 *   h=%r8 h2=%r9 n=%r10 n0=%rcx n1=%r10 aux=%r14
 *
 * 逐条翻译，没有代数化简。几处「看起来像笔误、但必须照抄」的地方就地标注。
 *
 * 全局量（定义在 montgomery_mul.c，汇编里靠 .comm 合并）：
 *   montgomery_modulo_n  指向 N 的 128 位表示
 *   montgomery_inv_n     = -N^-1 mod 2^64（montgomery_inverse() 返回 -inv）
 *
 * 契约：
 *   N 为奇数（低字为奇数即够）；除 mulm 外形参按模 N 使用，即 < N；
 *   mulm 的两个乘数 < N，此时结果就是 f1*f2*2^-128 mod N。
 *   越界输入汇编不崩，只是返回值不再是「模 N 的结果」，差分测试照样逐位可比。
 */

#include <stdint.h>
#include <x86intrin.h>

#include "siever-config.h"
#include "montgomery_mul.h"

extern ulong montgomery_inv_n;
extern ulong *montgomery_modulo_n;

/* 见 asm_arith64.cpp 里的同名说明：x 前缀是为了跟 noasm*.c 里的
 * asm_sub_n128 并存，头文件声明的是后者。 */
extern "C" void xasm_sub_n128(ulong *, ulong *);

typedef unsigned long long u64;

/* mulq：无符号 128 位乘，返回低半，高半写进 *hi。
 * 内部一律 u64（= unsigned long long），写回 ulong 数组前不换指针类型，
 * 免得 unsigned long / unsigned long long 之间踩严格别名。 */
static inline u64 mul_wide(u64 a, u64 b, u64 *hi)
{
	__uint128_t p = (__uint128_t)a * b;

	*hi = (u64)(p >> 64);
	return (u64)p;
}

/* addq/adc 的一格：acc + val + carry，结果留在 acc，传出的进位写进 *cout。
 * 汇编里连续的 addq/adcq 链就用它一格一格展开。 */
static inline u64 add_word(u64 acc, u64 val, unsigned char carry, unsigned char *cout)
{
	u64 r;

	*cout = _addcarry_u64(carry, acc, val, &r);
	return r;
}

/* 纯 addq：**不**吃上一条指令留下的进位，只把自己产生的进位交出去。
 * 这些内核里每一组累加的第一格都是 addq 而不是 adcq，前一组算出来的
 * 进位在这一格被直接丢掉。写成 add_word(acc, val, c, &c) 就错了。 */
static inline u64 add_plain(u64 acc, u64 val, unsigned char *cout)
{
	u64 r;

	*cout = _addcarry_u64(0, acc, val, &r);
	return r;
}

/* sub/sbb 的一格，同上。 */
static inline u64 sub_word(u64 acc, u64 val, unsigned char borrow, unsigned char *bout)
{
	u64 r;

	*bout = _subborrow_u64(borrow, acc, val, &r);
	return r;
}

/* --- asm_zero128(a): a = 0 --- */
void asm_zero128(ulong *a)
{
	a[0] = 0;
	a[1] = 0;
}

/* --- asm_copy128(b,a): b = a --- */
void asm_copy128(ulong *b, ulong *a)
{
	b[0] = a[0];
	b[1] = a[1];
}

/* --- asm_sub128_3(c,a,b): c = a - b mod N --- */
void asm_sub128_3(ulong *c, ulong *a, ulong *b)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	u64 r0 = a[0], r1 = a[1];
	unsigned char borrow = 0;

	r0 = sub_word(r0, (u64)b[0], borrow, &borrow);	/* subq (op2),%rax */
	r1 = sub_word(r1, (u64)b[1], borrow, &borrow);	/* sbbq 8(op2),%rcx */

	/* 两条 cmovcq 判的就是上面 sbbq 的借位 —— 中间只有 movq，不改标志位。 */
	unsigned char carry = 0;
	r0 = add_word(r0, borrow ? n0 : 0, carry, &carry);
	r1 = add_word(r1, borrow ? n1 : 0, carry, &carry);

	c[0] = r0;
	c[1] = r1;
}

/* 128 位整体右移一位，carry_in 是要补进最高字的那一位。
 * 对应汇编里 rcrq $1,%rdx ; rcq $1,%rax 这一串：从高到低逐字移，
 * 每一字补进来的都是**上一个字移位前**的第 0 位 —— 先把 hi 覆盖掉、
 * 再拿新的 hi 去补低位，就错了。 */
static inline void shr128(u64 *lo, u64 *hi, u64 carry_in)
{
	const u64 hib = *hi & 1;

	*hi = (*hi >> 1) | (carry_in << 63);
	*lo = (*lo >> 1) | (hib << 63);
}

/* --- asm_half128(a): a /= 2 mod N --- */
void asm_half128(ulong *a)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	u64 lo = a[0], hi = a[1];
	unsigned char c;

	if (lo & 1) {		/* testq $1 ; jnz half_odd128 */
		/* a 为奇数：先算 (a+N)，再右移一位。加法链出来的进位
		 * （128 位加是否溢出）不进位流，但正好是 rcrq $1,%rdx 要
		 * 补进 hi 的那一位。 */
		c = 0;
		lo = add_word(lo, n0, c, &c);		/* addq (%r8),%rax  */
		hi = add_word(hi, n1, c, &c);		/* adcq 8(%r8),%rcx */
		shr128(&lo, &hi, c);
	} else {
		/* a 为偶数：直接右移。shrq $1,%rdx 把 hi 最低位移出去当进位，
		 * 那一位由 rcq $1,%rax 补进 lo 的最高位；hi 自己没有东西要补，
		 * 所以 carry_in 是 0。 */
		shr128(&lo, &hi, 0);			/* shrq $1,%rdx ; rcq $1,%rax */
	}
	a[0] = lo;
	a[1] = hi;
}

/* --- xasm_sub_n128(b,a): b -= a mod 2^128 ---
 *
 * 汇编只有一条 subq，动的是低字，高字原封不动。原文件那行注释
 * "b-=a  mod 2^128???????????????????" 就是作者自己也在犹豫；
 * 但这不是 bug：这个 x 前缀的名字没人引用（真正在用的是 noasm64.c
 * 里那个做完整多字减法的 asm_sub_n128）。搬过来必须原样保留。 */
void xasm_sub_n128(ulong *b, ulong *a)
{
	b[0] = b[0] - a[0];
}

/* --- asm_add128(b,a): b += a mod N --- */
void asm_add128(ulong *b, ulong *a)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	u64 r0 = b[0], r1 = b[1];
	unsigned char borrow = 0, carry = 0;

	r0 = sub_word(r0, n0, borrow, &borrow);	/* subq %r8,%rax */
	r1 = sub_word(r1, n1, borrow, &borrow);	/* sbbq %r9,%rdx —— 这个借位没人用 */

	r0 = add_word(r0, (u64)a[0], carry, &carry);	/* addq (op),%rax   */
	r1 = add_word(r1, (u64)a[1], carry, &carry);	/* adcq 8(op),%rdx  */

	/* 两条 cmovncq 判的是这条 adcq 的进位，不是上面 sbbq 的借位。
	 * b < N 时这个进位等价于 (b + a) >= N，于是结果正好是 (b + a) mod N。
	 * 写成「if (b >= N) ...」是错的。 */
	if (!carry) {
		carry = 0;
		r0 = add_word(r0, n0, carry, &carry);
		r1 = add_word(r1, n1, carry, &carry);
	}
	b[0] = r0;
	b[1] = r1;
}

/* --- asm_mulm128(prod,f1,f2): prod = f1*f2*R^-1 mod N，R = 2^128 ---
 *
 * 整体是「按列乘法 + 两轮约简」：先把 f1*f2 的低两列累加进累加器，
 * 每轮约简往低字加上 N*inv_n（这一步把低 64 位消成 0 的效果来自
 * N[0]*inv_n = -1），第二轮用 imulq 从当前字算出蒙哥马利乘数再减 N。
 *
 * 两个必须照抄的地方：
 *
 * 1) imulq %r11,%r14 是**两操作数**形式（0F AF），只产生低 64 位，
 *    rdx 保持不动 —— 所以它必须在 mulq 之后用，上一条 mulq 的高半
 *    还能继续用。用成一位操作数的 mulq（求 128 位乘积）就全错了。
 *
 * 2) m4 里 define(res3,%r11) 和 define(res0,%r11) 是同一个寄存器，
 *    低字兼作最高位的进位槽。原汇编那行注释写 "res3=res0=0"，
 *    但实际上第一轮约简后 res0 = t0 - 1（因为 N[0]*inv_n ≡ -1），
 *    只有 t0 == 1 时才是 0。注释不准，代码是对的（400 组随机向量
 *    与教科书 Montgomery 逐位相符）。别照着注释去「修正」。
 */
void asm_mulm128(ulong *prod, ulong *f1, ulong *f2)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	const u64 h = f2[0], h2 = f2[1];
	u64 aux, res0, res1, res2;
	u64 &res3 = res0;	/* res3 就是 res0 那个寄存器，不是第四个独立字 */
	u64 lo, hi;
	unsigned char c;

	aux = montgomery_inv_n;	/* movq montgomery_inv_n(%rip),%r14 */

	/* movq (%rdx),%r8 ; movq 8(%rdx),%r9 : h = f2[0], h2 = f2[1] */

	/* --- first multiplication --- */
	lo = mul_wide(f1[0], h, &hi);	/* movq (%rsi),%rax ; mulq %r8 */
	res0 = lo;
	res1 = hi;

	lo = mul_wide(f1[1], h, &hi);	/* movq 8(%rsi),%rax ; mulq %r8 */

	/* imulq %r11,%r14 是**两操作数**形式：结果只留低 64 位写进 %r14，
	 * %rax / %rdx 一位不动（它们还装着上面那条 mulq 的积）。
	 * 所以 %r14 从此变成蒙哥马利乘数 m0，下面第一轮约简的 mulq %r14
	 * 乘的是 m0 而不是 inv_n；同时累加器加的仍然是 f1[1]*f2[0]。 */
	aux = aux * res0;
	res2 = 0;			/* movq $0,%r12 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r13 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r12 */

	/* --- first reduction：把 N*aux 加进来（低字消掉） --- */
	lo = mul_wide(n0, aux, &hi);	/* movq %rcx,%rax ; mulq %r14 */
	res0 = add_plain(res0, lo, &c);	/* addq %rax,%r11 */
	res1 = add_word(res1, hi, c, &c);	/* adcq %rdx,%r13 */
	res2 = add_word(res2, 0, c, &c);		/* adcq $0,%r12 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */

	lo = mul_wide(n1, aux, &hi);	/* movq %r10,%rax ; mulq %r14 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r13 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r12 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */

	/* --- second multiplication：再乘一列 f2[1] --- */
	lo = mul_wide(f1[0], h2, &hi);	/* movq (%rsi),%rax ; mulq %r9 */
	aux = montgomery_inv_n;			/* movq montgomery_inv_n(%rip),%r14 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r13 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r12 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */

	lo = mul_wide(f1[1], h2, &hi);	/* movq 8(%rsi),%rax ; mulq %r9 */
	aux = aux * res1;		/* imulq %r13,%r14：只写 %r14 */

	/* subq %rcx,%r12 ; sbbq %r10,%r11 算出来的借位没人用：
	 * 紧接着的 addq %rax,%r12 是纯加法，把借位丢掉了，后面的 cmovcq
	 * 也不判它。 */
	c = 0;
	res2 = sub_word(res2, n0, c, &c);		/* subq %rcx,%r12 */
	res3 = sub_word(res3, n1, c, &c);		/* sbbq %r10,%r11 */

	res2 = add_plain(res2, lo, &c);		/* addq %rax,%r12 */
	res3 = add_word(res3, hi, c, &c);		/* adcq %rdx,%r11 */

	/* --- second reduction（同样乘改写后的 %r14） --- */
	lo = mul_wide(n0, aux, &hi);	/* movq %rcx,%rax ; mulq %r14 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r13 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r12 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */

	lo = mul_wide(n1, aux, &hi);	/* movq %r10,%rax ; mulq %r14 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r12 */
	/* cmovcq 判的是这条 adcq 的进位 —— 又是「最近一条改标志位的指令」。 */
	res3 = add_word(res3, hi, c, &c);

	/* cmovcq %r13,%rcx / cmovcq %r13,%r10：有进位才把 res1 搬过来，
	 * 没进位时 %rcx / %r10 保持原值 —— 原值是 movq (%r10),%rcx 装的 N[0]
	 * 和 movq 8(%r10),%r10 装的 N[1]，不是 res1。条件写反就全错。 */
	const u64 r0 = c ? res1 : n0;
	const u64 r1 = c ? res1 : n1;

	prod[0] = add_plain(r0, res2, &c);	/* addq %r12,%rcx */
	prod[1] = add_word(r1, res3, c, &c);	/* adcq %r11,%r10 */
}