/* asm_arith192 —— 192 位蒙哥马利模 N 算术内核的 C++ 版。
 *
 * 由 factor/nfs/lasieve/asm/asm_arith192.asm 翻译而来
 *（Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL）。
 * 原文件是 m4 模板，寄存器别名来自文件头的 define(...)：
 *
 *   rop=%rdi op=%rsi op1=%rsi op2=%rdx op2x=%rcx
 *   res0=%r9 res1=%r10 res2=%r14 res3=%r11
 *   res4=%r9   <-- 和 res0 是同一个寄存器
 *   res5=%r10  <-- 和 res1 是同一个寄存器
 *   h=%r8 n=%rbx n0=%r12 n1=%r15 n2=%rbx aux=%r13
 *
 * 逐条翻译，没有代数化简。几处「看起来像笔误、但必须照抄」的地方就地标注。
 *
 * 全局量（定义在 montgomery_mul.c，汇编里靠 .comm 合并）：
 *   montgomery_modulo_n  指向 N 的 192 位表示
 *   montgomery_inv_n     = -N^-1 mod 2^64（montgomery_inverse() 返回 -inv）
 *
 * 契约：
 *   N 为奇数；除 mulm 外形参按模 N 使用，即 < N；mulm 的两个乘数 < N，
 *   此时结果就是 f1*f2*2^-192 mod N。
 *   越界输入汇编不崩，只是返回值不再是「模 N 的结果」，差分测试照样逐位可比。
 */

#include <stdint.h>
#include <x86intrin.h>

#include "siever-config.h"
#include "montgomery_mul.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
extern ulong montgomery_inv_n;
extern ulong *montgomery_modulo_n;

/* 见 asm_arith64.cpp 里的同名说明：x 前缀是为了跟 noasm*.c 里的
 * asm_sub_n192 并存，头文件声明的是后者。 */
void xasm_sub_n192(ulong *, ulong *);

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

/* adcq 的一格：acc + val + carry，结果留在 acc，传出的进位写进 *cout。 */
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

/* 192 位整体右移一位，carry_in 是要补进最高字的那一位。
 * 对应 shrq $1,%rdx ; rcrq $1,%rcx ; rcrq $1,%rax 这一串：从高到低逐字移，
 * 每一字补进来的都是**上一个字移位前**的第 0 位 —— 先把上一字覆盖掉、
 * 再拿新的值去补低位，就错了。 */
static inline void shr192(u64 *lo, u64 *m1, u64 *m2, u64 carry_in)
{
	const u64 b2 = *m2 & 1;
	const u64 b1 = *m1 & 1;

	*m2 = (*m2 >> 1) | (carry_in << 63);
	*m1 = (*m1 >> 1) | (b2 << 63);
	*lo = (*lo >> 1) | (b1 << 63);
}

/* --- asm_zero192(a): a = 0 --- */
void asm_zero192(ulong *a)
{
	a[0] = 0;
	a[1] = 0;
	a[2] = 0;
}

/* --- asm_copy192(b,a): b = a --- */
void asm_copy192(ulong *b, ulong *a)
{
	b[0] = a[0];
	b[1] = a[1];
	b[2] = a[2];
}

/* --- asm_sub192_3(c,a,b): c = a - b mod N --- */
void asm_sub192_3(ulong *c, ulong *a, ulong *b)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	const u64 n2 = montgomery_modulo_n[2];
	u64 r0 = a[0], r1 = a[1], r2 = a[2];
	unsigned char borrow = 0, carry = 0;

	r0 = sub_word(r0, (u64)b[0], borrow, &borrow);	/* subq (op2),%rax  */
	r1 = sub_word(r1, (u64)b[1], borrow, &borrow);	/* sbbq 8(op2),%rcx */
	r2 = sub_word(r2, (u64)b[2], borrow, &borrow);	/* sbbq 16(op2),%r10 */

	/* 三条 cmovcq 判的是最后那条 sbbq 的借位 —— 中间只有 movq。 */
	r0 = add_word(r0, borrow ? n0 : 0, carry, &carry);
	r1 = add_word(r1, borrow ? n1 : 0, carry, &carry);
	r2 = add_word(r2, borrow ? n2 : 0, carry, &carry);

	c[0] = r0;
	c[1] = r1;
	c[2] = r2;
}

/* --- asm_half192(a): a /= 2 mod N --- */
void asm_half192(ulong *a)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	const u64 n2 = montgomery_modulo_n[2];
	u64 lo = a[0], m1 = a[1], m2 = a[2];
	unsigned char c;

	if (lo & 1) {		/* testq $1 ; jnz half_odd192 */
		/* a 为奇数：先算 (a+N)，再右移一位。加法链出来的进位
		 * （192 位加是否溢出）不进位流，但正好是 rcrq $1,%rdx 要
		 * 补进最高字的那一位。 */
		c = 0;
		lo = add_word(lo, n0, c, &c);		/* addq (%r8),%rax  */
		m1 = add_word(m1, n1, c, &c);		/* adcq 8(%r8),%rcx */
		m2 = add_word(m2, n2, c, &c);		/* adcq 16(%r8),%rdx */
		shr192(&lo, &m1, &m2, c);
	} else {
		/* a 为偶数：直接右移。shrq $1,%rdx 把最高字最低位移出去当进位，
		 * 那一位由 rcrq $1,%rcx 补进中字；最高字自己没有东西要补，
		 * 所以 carry_in 是 0。 */
		shr192(&lo, &m1, &m2, 0);
	}
	a[0] = lo;
	a[1] = m1;
	a[2] = m2;
}

/* --- xasm_sub_n192(b,a): b -= a mod 2^192 ---
 *
 * 汇编只有一条 subq，只动低字。原文件那行注释
 * "b-=a  mod 2^192???????????????????" 就是作者自己也在犹豫；
 * 但这不是 bug：这个 x 前缀的名字没人引用（真正在用的是 noasm64.c
 * 里那个做完整多字减法的 asm_sub_n192）。搬过来必须原样保留。 */
void xasm_sub_n192(ulong *b, ulong *a)
{
	b[0] = b[0] - a[0];
}

/* --- asm_add192(b,a): b += a mod N --- */
void asm_add192(ulong *b, ulong *a)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	const u64 n2 = montgomery_modulo_n[2];
	u64 r0 = b[0], r1 = b[1], r2 = b[2];
	unsigned char borrow = 0, carry = 0;

	r0 = sub_word(r0, n0, borrow, &borrow);	/* subq %r8,%rax  */
	r1 = sub_word(r1, n1, borrow, &borrow);	/* sbbq %r9,%rcx  */
	r2 = sub_word(r2, n2, borrow, &borrow);	/* sbbq %r10,%rdx —— 这个借位没人用 */

	r0 = add_plain(r0, (u64)a[0], &carry);		/* addq (op),%rax   */
	r1 = add_word(r1, (u64)a[1], carry, &carry);	/* adcq 8(op),%rcx  */
	r2 = add_word(r2, (u64)a[2], carry, &carry);	/* adcq 16(op),%rdx */

	/* 这一版是先 movq $0,%r11 再 cmovcq %r11,%r8/%r9/%r10：有进位就把
	 * N 的三个字清零，没有进位就保持 N —— 等价于「没有进位才加 N」。
	 * 三条 cmov 判的是最后那条 adcq 的进位，不是上面 sbbq 的借位。
	 * b < N 时这个进位等价于 (b + a) >= N，于是结果正好是 (b + a) mod N。 */
	if (!carry) {
		carry = 0;
		r0 = add_plain(r0, n0, &carry);		/* addq %r8,%rax  */
		r1 = add_word(r1, n1, carry, &carry);	/* adcq %r9,%rcx  */
		r2 = add_word(r2, n2, carry, &carry);	/* adcq %r10,%rdx */
	}
	b[0] = r0;
	b[1] = r1;
	b[2] = r2;
}

/* --- asm_mulm192(prod,f1,f2): prod = f1*f2*R^-1 mod N，R = 2^192 ---
 *
 * 按列乘法：f1*f2 分三列累加，每列累完加一次 N*aux 做约简，最后一列再
 * 用 imulq 算出蒙哥马利乘数、减掉 N，然后做第三轮约简。
 *
 * 四个必须照抄的地方：
 *
 * 1) imulq %r9,%r13 是**两操作数**形式（0F AF）：结果只留低 64 位写进
 *    %r13（aux），%rax / %rdx 一位不动 —— 它们还装着这一列的乘积。
 *    所以「加进累加器的是列乘积 low/high」，同时 aux 从此变成蒙哥马利
 *    乘数，后面那几条 mulq %r13 乘的是 aux 而不是 inv_n。写成一位操作数的
 *    mulq，或者拿 inv_n 去乘 N，全错。
 *
 * 2) 每一组累加的**第一格是纯 addq**，不吃上一条指令留下的进位/借位；
 *    第三轮里紧跟 sbbq 的那条 addq %rax,%r9 同样把借位丢掉了。
 *
 * 3) m4 里 define(res4,%r9) 与 define(res0,%r9)、define(res5,%r10) 与
 *    define(res1,%r10) 分别是同一个寄存器：低两位兼作累加器最高两位的
 *    进位槽，而最后的输出恰好又是 (res3, res4, res5) = (res3, res0, res1)。
 *    这里用引用把别名关系写死，不要「顺手」拆成独立变量。
 *
 * 4) 末尾三条 cmovcq：有进位才把 res2 搬进 %r12/%r15/%rbx；没进位时它们
 *    保持原值，原值是 N 的三个字，不是 res2。条件写反就全错。
 */
void asm_mulm192(ulong *prod, ulong *f1, ulong *f2)
{
	const u64 n0 = montgomery_modulo_n[0];
	const u64 n1 = montgomery_modulo_n[1];
	const u64 n2 = montgomery_modulo_n[2];
	u64 res0, res1, res2, res3;
	u64 &res4 = res0;	/* res4 就是 res0 那个寄存器 */
	u64 &res5 = res1;	/* res5 就是 res1 那个寄存器 */
	u64 aux, h = f2[0], lo, hi;
	unsigned char c;

	aux = montgomery_inv_n;	/* movq montgomery_inv_n(%rip),%r13 */

	/* --- first multiplication --- */
	lo = mul_wide(f1[0], h, &hi);	/* movq (%rsi),%rax ; mulq %r8 */
	res0 = lo;
	res1 = hi;

	lo = mul_wide(f1[1], h, &hi);	/* movq 8(%rsi),%rax ; mulq %r8 */
	res2 = 0;			/* movq $0,%r14 */
	res3 = 0;			/* movq $0,%r11 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r10 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r14 */

	lo = mul_wide(f1[2], h, &hi);	/* movq 16(%rsi),%rax ; mulq %r8 */
	aux = aux * res0;		/* imulq %r9,%r13：只写 %r13 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */

	/* --- first reduction --- */
	lo = mul_wide(n0, aux, &hi);	/* movq %r12,%rax ; mulq %r13 */
	res0 = add_plain(res0, lo, &c);	/* addq %rax,%r9  */
	res1 = add_word(res1, hi, c, &c);	/* adcq %rdx,%r10 */
	res2 = add_word(res2, 0, c, &c);		/* adcq $0,%r14 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */

	lo = mul_wide(n1, aux, &hi);	/* movq %r15,%rax ; mulq %r13 */
	res1 = add_plain(res1, lo, &c);		/* addq %rax,%r10 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r14 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */

	lo = mul_wide(n2, aux, &hi);	/* movq %rbx,%rax ; mulq %r13 */
	h = f2[1];			/* movq 8(%rcx),%r8 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */

	/* --- second multiplication --- */
	lo = mul_wide(f1[0], h, &hi);	/* movq (%rsi),%rax ; mulq %r8 */
	res1 = add_plain(res1, lo, &c);		/* addq %rax,%r10 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r14 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */

	lo = mul_wide(f1[1], h, &hi);	/* movq 8(%rsi),%rax ; mulq %r8 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */

	lo = mul_wide(f1[2], h, &hi);	/* movq 16(%rsi),%rax ; mulq %r8 */
	aux = montgomery_inv_n;			/* movq montgomery_inv_n(%rip),%r13 */
	aux = aux * res1;			/* imulq %r10,%r13：只写 %r13 */
	res3 = add_plain(res3, lo, &c);		/* addq %rax,%r11 */
	res4 = add_word(res4, hi, c, &c);	/* adcq %rdx,%r9  */

	/* --- second reduction --- */
	lo = mul_wide(n0, aux, &hi);	/* movq %r12,%rax ; mulq %r13 */
	res1 = add_plain(res1, lo, &c);	/* addq %rax,%r10 */
	res2 = add_word(res2, hi, c, &c);	/* adcq %rdx,%r14 */
	res3 = add_word(res3, 0, c, &c);		/* adcq $0,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(n1, aux, &hi);	/* movq %r15,%rax ; mulq %r13 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(n2, aux, &hi);	/* movq %rbx,%rax ; mulq %r13 */
	h = f2[2];			/* movq 16(%rcx),%r8 */
	res3 = add_plain(res3, lo, &c);	/* addq %rax,%r11 */
	res4 = add_word(res4, hi, c, &c);	/* adcq %rdx,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	/* --- third multiplication --- */
	lo = mul_wide(f1[0], h, &hi);	/* movq (%rsi),%rax ; mulq %r8 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(f1[1], h, &hi);	/* movq 8(%rsi),%rax ; mulq %r8 */
	res3 = add_plain(res3, lo, &c);	/* addq %rax,%r11 */
	res4 = add_word(res4, hi, c, &c);	/* adcq %rdx,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(f1[2], h, &hi);	/* movq 16(%rsi),%rax ; mulq %r8 */
	aux = montgomery_inv_n;			/* movq montgomery_inv_n(%rip),%r13 */
	aux = aux * res2;			/* imulq %r14,%r13：只写 %r13 */

	/* subq/sbbq/sbbq 算出来的借位没人用：紧接着的 addq %rax,%r9 是纯加法，
	 * 把借位丢掉了；末尾三条 cmovcq 也不判它。 */
	c = 0;
	res3 = sub_word(res3, n0, c, &c);	/* subq %r12,%r11 */
	res4 = sub_word(res4, n1, c, &c);	/* sbbq %r15,%r9  */
	res5 = sub_word(res5, n2, c, &c);	/* sbbq %rbx,%r10 */

	res4 = add_plain(res4, lo, &c);	/* addq %rax,%r9  */
	res5 = add_word(res5, hi, c, &c);	/* adcq %rdx,%r10 */

	/* --- third reduction --- */
	lo = mul_wide(n0, aux, &hi);	/* movq %r12,%rax ; mulq %r13 */
	res2 = add_plain(res2, lo, &c);	/* addq %rax,%r14 */
	res3 = add_word(res3, hi, c, &c);	/* adcq %rdx,%r11 */
	res4 = add_word(res4, 0, c, &c);		/* adcq $0,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(n1, aux, &hi);	/* movq %r15,%rax ; mulq %r13 */
	res3 = add_plain(res3, lo, &c);	/* addq %rax,%r11 */
	res4 = add_word(res4, hi, c, &c);	/* adcq %rdx,%r9  */
	res5 = add_word(res5, 0, c, &c);		/* adcq $0,%r10 */

	lo = mul_wide(n2, aux, &hi);	/* movq %rbx,%rax ; mulq %r13 */
	res4 = add_plain(res4, lo, &c);	/* addq %rax,%r9  */
	/* 末尾三条 cmovcq 判的是这条 adcq 的进位。 */
	res5 = add_word(res5, hi, c, &c);	/* adcq %rdx,%r10 */

	/* cmovcq %r14,%r12 / %r15 / %rbx：有进位才把 res2 搬过来，
	 * 没进位时这三个寄存器保持原值 —— 原值是 N 的三个字。 */
	const u64 r0 = c ? res2 : n0;		/* cmovcq %r14,%r12 */
	const u64 r1 = c ? res2 : n1;		/* cmovcq %r14,%r15 */
	const u64 r2 = c ? res2 : n2;		/* cmovcq %r14,%rbx */

	prod[0] = add_plain(r0, res3, &c);	/* addq %r11,%r12 */
	prod[1] = add_word(r1, res4, c, &c);	/* adcq %r9,%r15  */
	prod[2] = add_word(r2, res5, c, &c);	/* adcq %rbx,%r10 */
	/* 汇编最后存的是 (res3, res4, res5) = (res3, res0, res1)，
	 * 这里 prod[0..2] 就是这三格。 */
}
}  /* namespace lasieve_ns */
