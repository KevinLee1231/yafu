/* asm_arith64 —— 64 位蒙哥马利模 N 算术内核的 C++ 版。
 *
 *（Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL）。
 * 参数寄存器别名：
 *
 *   rop=%rdi  op=%rsi  op1=%rsi  op2=%rdx
 *
 * 刻意没有做代数化简。三处「看起来像笔误、但必须照抄」
 * 的地方在下面各自标注了原因。
 *
 * 全局量（由 montgomery_mul.cpp 定义）：
 *   montgomery_modulo_n  指向 N 的 64 位表示的指针（不是 N 本身）
 *   montgomery_inv_n     = -N^-1 mod 2^64，见 montgomery_inverse() 的 return (-inv)
 *
 * 契约（真实调用点恒满足）：
 *   N 为奇数；除 mulm 外，形参都按「模 N 意义下的数」使用，即 < N；
 *   mulm 的两个乘数 < N；asm_half64 里 a + N 不得溢出 64 位，
 *   即要求 N <= 2^63（筛法里的 64 位 N 就是这个量级）。
 *   越界输入汇编不会崩（这几条都是纯加减乘），只是返回值不再是「模 N 的结果」，
 *   差分测试照样逐位可比。
 */

#include <stdint.h>
#include <x86intrin.h>

#include "siever-config.h"
#include "montgomery_mul.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
extern ulong montgomery_inv_n;
extern ulong *montgomery_modulo_n;

/* 汇编导出的是 xasm_sub_n64，头文件里声明的却是 noasm64.c 里的 asm_sub_n64
 * —— 汇编里的 x 前缀就是为了让这个名字跟 C 版并存（两边行为并不一样，见
 * xasm_sub_n64 的注释）。所以这三个名字得在这里自己 声明一次，
 * 链接名才和汇编一致。 */
void xasm_sub_n64(ulong *, ulong *);

typedef unsigned long long u64;

/* mulq：无符号 128 位乘，返回低半，高半写进 *hi。
 * 内部一律用 u64（= unsigned long long），最后再写回 ulong 数组，
 * 免得 unsigned long / unsigned long long 之间来回换指针碰严格别名。 */
static inline u64 mul_wide(u64 a, u64 b, u64 *hi)
{
	__uint128_t p = (__uint128_t)a * b;

	*hi = (u64)(p >> 64);
	return (u64)p;
}

/* --- asm_zero64(a): a = 0 --- */
void asm_zero64(ulong *a)
{
	a[0] = 0;
}

/* --- asm_copy64(b,a): b = a --- */
void asm_copy64(ulong *b, ulong *a)
{
	b[0] = a[0];
}

/* --- asm_sub64_3(c,a,b): c = a - b mod N --- */
void asm_sub64_3(ulong *c, ulong *a, ulong *b)
{
	const u64 n = montgomery_modulo_n[0];
	u64 r;

	/* subq (op2),%rax 产生借位，紧接着的 cmovcq (%r9),%r8 判的就是它，
	 * 中间没有别的改标志位的指令。 */
	const unsigned char borrow = _subborrow_u64(0, (u64)a[0], (u64)b[0], &r);

	if (borrow)
		r += n;		/* addq %r8,%rax */
	c[0] = r;
}

/* --- asm_half64(a): a /= 2 mod N --- */
void asm_half64(ulong *a)
{
	const u64 n = montgomery_modulo_n[0];
	u64 r = a[0];

	/* testq $1,%rax 只设 ZF；cmovnzq 在 ZF=0 时才搬 N，即 a 为奇数才加 N。
	 * 判的是 test 的 ZF，不是任何一条算术指令的进位。 */
	const unsigned char c = _addcarry_u64(0, r, (r & 1) ? n : 0, &r);

	/* rcr $1,%rax：进位来自上面那条 addq（a+N 是否溢出），
	 * 也就是 (a+N) 的第 64 位，正好是右移一位要补进来的那一位。 */
	r = (r >> 1) | ((u64)c << 63);
	a[0] = r;
}

/* --- xasm_sub_n64(b,a): b -= a mod 2^64 ---
 *
 * 汇编里就这一条 movq/subq/movq/ret。128/192 位版本同样只动低字
 * （原文件注释写的是 "b-=a mod 2^128???????????????????"），
 * 那不是笔误也不是 bug：这个 x 前缀的名字没人引用，真正的 asm_sub_n64
 * 在 noasm64.c 里做的是完整的多字减法。搬过来必须原样保留。 */
void xasm_sub_n64(ulong *b, ulong *a)
{
	b[0] = b[0] - a[0];
}

/* --- asm_add64(b,a): b += a mod N --- */
void asm_add64(ulong *b, ulong *a)
{
	const u64 n = montgomery_modulo_n[0];
	u64 r, s;

	/* subq %r9,%rax 的借位（b < N）算出来了，但紧接着的 addq 把标志位
	 * 冲掉了，后面没人再用它。所以这一步对结果没有影响。 */
	r = (u64)b[0] - n;

	/* 关键：cmovncq %r9,%r8 判的是这条 addq 的进位，不是上面那条 subq
	 * 的借位。b < N 时这条进位等价于 (b + a) >= N，于是结果正好是
	 * (b + a) mod N。写成「if (b >= N) r += n」是错的。 */
	const unsigned char carry = _addcarry_u64(0, r, (u64)a[0], &s);

	if (!carry)
		s += n;		/* addq %r8,%rax */
	b[0] = s;
}

/* --- asm_mulm64(prod,f1,f2): prod = f1*f2*R^-1 mod N，R = 2^64 --- */
void asm_mulm64(ulong *prod, ulong *f1, ulong *f2)
{
	const u64 n = montgomery_modulo_n[0];
	const u64 inv = montgomery_inv_n;
	u64 t0, t1, m0, unused_hi, lo, hi, r, out;

	/* movq (op2),%rax ; mulq (op1) —— 注意乘的是 op2[0]，乘法交换律
	 * 掩盖了这一点，但顺序照抄。 */
	t0 = mul_wide((u64)f2[0], (u64)f1[0], &t1);

	/* mulq %r8：m = t0 * inv_n 的 128 位乘积。低半 m0 用来算 m0*N，
	 * 高半在这一条里算完就被下一条 mulq 冲掉了，汇编没用它。 */
	m0 = mul_wide(t0, inv, &unused_hi);

	lo = mul_wide(m0, n, &hi);

	r = lo + t0;			/* addq %r10,%rax */
	const unsigned char c1 = (r < lo) ? 1 : 0;

	/* adcq %r11,%rdx，r11 = t1 - N。这一步是 rdx + r11 + 进位 的**一次**
	 * 65 位加法：必须连同进位一起算，不能先算 hi + (t1-N) 再补进位 ——
	 * 前者会自己先溢出，那一位进位就丢了，而它正是下面 cmovnc 要判的。
	 * （r11 = t1 - N 是模 2^64 的减法结果。） */
	const unsigned char c2 = _addcarry_u64(c1, hi, t1 - n, &out);

	/* cmovncq %r9,%rax ：只有 c2 == 0 时才把 N 搬进 %rax。
	 * 注意 %rax 在这一步之前装的是 addq 的结果 r（= lo + t0），不是 0，
	 * 而后面那条 addq %rax,%rdx 是无条件执行的。所以 c2 == 1 时加进
	 * 结果里的是 r，不是 0 —— 这段千万别简化成 if (!c2) out += n。
	 *
	 * 契约内 r 恒等于 0：inv_n = -N^-1 mod 2^64 保证 m0*N ≡ -t0 (mod 2^64)，
	 * 于是 lo(m0*N) + t0 ≡ 0。加的其实是 0，所以看不出差别；只有 N 为偶数
	 * （契约外，inv_n 无意义）时 r 才非零，这时两边仍然必须逐位相同。 */
	out += c2 ? r : n;

	prod[0] = out;
}
}  /* namespace lasieve_ns */
