/* (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「排线」内核：给定一条加权筛线 (ri[0], ri[1]) 和该线在因子基里的当前
 * 位置 ij，把这条线上 ij < ij_ub 的每个筛法命中写进排线缓冲
 * sched_ptr[ij >> L1_BITS]，写完把溢出量 ij - ij_ub 存回 ij_ptr 传给调用方，
 * 返回值是消耗完的 ri（调用方靠这个推进下一条线）。
 *
 * 三个来源文件、一个内核、四种 oddness type：
 *
 *   lasched0.asm   → lasched0 / lasched0_1   （ot==0：不做 tester 选择）
 *   lasched1.asm -Dot=1 → lasched1   / lasched1_1
 *   lasched1.asm -Dot=2 → lasched2   / lasched2_1   （唯一带异常分支的）
 *   lasched1.asm -Dot=3 → lasched3   / lasched3_1
 *   上述四份再加 -Dnt_sched=1 → 各自的 *nt 变体
 *
 * kernels/lasched3.asm **没有搬**：全仓库（Makefile、源码、脚本）里没有任何地方
 * 引用它，Makefile 的 lasched3I%.s 规则是从 lasched1.asm 用 -Dot=3 生成的。
 * 而且那份 lasched3.asm 是 lasched1.asm 的旧版本（只有入口没有 _1/nt 变体、
 * 直接用 %rdx 而不把第三个参数压栈、初值算法是另一套写法），真去构建它还会
 * 和 lasched1.asm -Dot=3 抢同一个符号 lasched3。所以它是被取代的遗留文件。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) 循环边界是拿**指针的数值**比出来的，而且两处比较用的下界不一样：
 *
 *      入口:  cmpq %rsi,%rdx ; jbe      →  %rdx(arg3) <= %rsi(arg2) 就退出
 *      回边:  cmpq %rsi,(%rsp) ; ja      →  (%rsp) > %rsi 才继续
 *
 *    回边那个 %rsp 指向的是入口 `pushq ij_ptr_ub_arg` 压进去的
 *    `leaq -4(%rdx),%rdx`，也就是**第三个参数减 4 字节**（4 字节 = 1 个 u32
 *    元素，不是 1 个指针步长）；而入口那条比的是未减 4 的 %rdx。换成元素
 *    说法：入口是「arg2 < arg3 才进」，回边是「自增前的 ij_ptr + 1 < arg3
 *    才继续」。两者合起来正好处理 (arg3-arg2)/4 个元素 —— 入口用未减的界、
 *    回边用减过的界，差的这一个元素让最后一次回边判断落在 arg3-4 上，正好
 *    停住，两个写法在这里是自洽的，不要因为它们长得不一样就当成 n-1。
 *    对比之下容易看错的是 ri/ij_ptr 的推进位置（见第 10 条）。
 *
 * 2) 内层两个条件跳转的方向。汇编模板是 AT&T 语法（op src,dst），比较指令
 *    的第一个操作数是**源**，第二个是**目的**，标志位反映「目的 CMP 源」：
 *
 *      cmpl %ebx,%r14d   →  a CMP i   ；cmovbe → a <= i  → 加 ri[1]
 *      cmpq %rbx,%r15    →  b CMP i   ；cmoval  → b >  i  → 加 ri[0]
 *
 *    也就是 C 参考里那两行 `if (i < b) ij += ri[0]; if (i >= a) ij += ri[1];`。
 *    这两个 cmov 各自紧跟在自己的比较后面（中间插的 mov/or 不改标志位），
 *    但 `orl %r9d,%r11d` 会改标志位，所以 `cmoval` 必须在它之后 —— 现有
 *    顺序已经是对的，照抄顺序即可，不要重排。
 *
 * 3) a / b 是**取负后再掩码**：`negl %r14d ; andl $1023,%r14d`。它等于
 *    n_i - (ri[x] & (n_i-1))，但 ri[x]&(n_i-1)==0 时结果是 0 而不是 n_i。
 *    写成 n_i - (ri[x] & mask) 会在那一个值上差 1。
 *
 * 4) a / b 存在 %r14d / %r15d 里，ri[0]/ri[1] 的原值另外留在 %edx(v0) /
 *    %ebp(v1) 供 cmov 用 —— 内层循环里 ri 指针不动，所以这两份快照全程有效。
 *    medsched0.asm 更省：直接 `cmovbel 4(%rdi)` / `cmoval (%rdi)` 重新读内存。
 *
 * 5) fbi_offs 只有 32 位存储版本用。asm 那侧是 `shrq $16,%r9` 入口先右移 16、
 *    回边 `addl $65536,%r9d` 每轮加 1（等价于 C 的 fbi_offs << 16 且每轮
 *    fbi_offs++，前提是 fbi_offs < 2^48）。
 *    而 lasched*_1 那几份**完全不用 fbi_offs**：入口没有 `shrq $16`、回边没有
 *    `addl $65536`、内层也没有 `orl %r9d,%r11d` —— 存出去的 16 位就是纯的
 *    ij & (L1_SIZE-1)，和 kernels/lasched.c 里那句注释一致（"we only store the
 *    2-byte masked ij instead of the 32-bit OR'd value above"）。
 *    这一点很容易看错成「fbi_offs 原样参与 or，只是被 movw 截断」—— 差分
 *    测试用低 16 位非零的 fbi_offs 一测就分开了。
 *
 * 6) lasched*_1 的 sched_ptr 元素当字节地址用：存 16 位 `movw`、指针
 *    `leaq 2` 前进 2 字节，但写回 sched_ptr 用的还是 64 位的 `movq`。
 *    所以参数类型虽然声明成 u32_t**，实际按 (unsigned char*) 走。
 *
 * 7) 入口那两条 movl 在 jbe 之前：循环一次都不跑的情况下 ij_ptr[0] 和
 *    ri[0]/ri[1] 已经被读过一次。读本身没有副作用，只是差分测试要保证这些
 *    地址可读（lasched0.asm 那一份另有一处栈问题，见第 11 条）。
 *
 * 8) 两个 popq %rbp：入口一共压了 7 个（rbx r12 r13 r14 r15 rbp 加第三个
 *    参数），`function_head` 在 linux 分支**不**压 rbp，所以第一个 popq %rbp
 *    是正常还原 rbp，第二个 popq %rbp 把那个压栈的第三参数丢掉。副作用是
 *    返回时 %rbp 已经被弄脏（违反 SysV 的 callee-saved 约定）—— 原汇编就
 *    是这样，本文件不复制这个行为，C++ 侧由编译器自己按 ABI 保存还原。
 *
 * 9) ij_ub 是 64 位移位（`shlq $n_i_bits,%rcx`）出来的，但内层的
 *    `cmpl %eax,%ecx`、`subl %ecx,%eax`、`cmpl %eax,%ecx` 全部只碰低 32 位，
 *    存回 ij_ptr 的也是 `ijd - (u32)ij_ub`。n1_j 大到让高 32 位非零时，
 *    结果和只用低 32 位算 ij_ub 一样 —— 但别写成 `n1_j << n_i_bits` 之后再
 *    拿 64 位去比，那会引入汇编里没有的高位比较。
 *
 * 10) ri 与 ij_ptr 是在退出判断**之前**推进的。回边是
 *     `leaq 8(ri),%ri` / `leaq 4(ij_ptr),%ij_ptr` 都排在 `ja` 前面，所以 ri
 *     每轮无条件前进 2 个元素、**包括最后一轮**，返回值是 ri + 2*n 而不是
 *     ri + 2*(n-1)；而 `cmpq %rsi,(%rsp)` 比的是自增之前的 ij_ptr。
 *
 * 11) lasched0.asm 那一份在 n == 0 时会毁栈，本文件**故意不复制**。
 *     lasched0.asm 的入口顺序是
 *         pushq %rbx ; pushq %r12 ; pushq %r13 ; pushq %r14
 *         jbe  ..._fbi_loop_end            <-- 只压了 4 个就跳走
 *         shrq $16,%r9
 *         pushq %r15 ; pushq %rbp ; pushq ij_ptr_ub_arg
 *     而收尾无条件 popq 7 次。于是 ij_ptr >= ij_ptr_ub（n <= 0）时提前退出，
 *     多吃掉的 3 个栈槽里就有调用者的返回地址，ret 于是跳到栈上的垃圾。
 *     实测 lasched0 / lasched0_1 用 n=0 调用直接段错误。
 *     lasched1.asm 没这个问题 —— 它那 7 次 push 全在 jbe 之前；
 *     medsched0.asm 同理（2 次 push 全在 jbe 之前）。
 *     真实调用点传的是 current_ij[s]+fbi_lb .. +fbi_ub 这一段，恒 n >= 1，
 *     所以这是一条用不到的路径。下面的 C++ 直接返回 ri（语义上就是空操作），
 *     顺带不再毁栈。
 *
 * 12) 每一轮的**起始 ij 从哪里来**，两个 .asm 不一样，这是最容易写错的一处：
 *     - lasched0.asm（ot==0）的回边是 `movl 4(%rsi),%eax`，从**输入数组**
 *       里重新读下一个元素当起始 ij。所以 ij_ptr[k] 写回去的 (ij - ij_ub)
 *       **不会**传给第 k+1 轮，每轮的起始 ij 各自来自 ij_ptr[k]。
 *       写成沿用上一轮的 ijd，n == 1 时看不出来，n >= 2 才暴露。
 *     - lasched1.asm（ot!=0）相反：回边不重读 ij_ptr[1]，ijd 留在寄存器里，
 *       但下一轮开头就被 tester 推导整个覆盖，所以也等价于每轮重算。
 *     C 参考实现两种都是「每轮开头 ij = *ij_ptr」，与汇编一致。
 *
 * ====================== 与 kernels/lasched_dispatch.cpp 的分歧 ======================
 *
 * lasched_dispatch.cpp 自带一套标量/AVX512 实现：#if !defined(AVX512_LASCHED)
 * 时它转调本文件的内核入口，定义了 AVX512_LASCHED 就用它自己那套。构建在
 * 启用 AVX-512 时会定义 AVX512_LASCHED，所以那条路才是生产路径，本文件的
 * 入口只在未启用 AVX-512 时被调用。两条路的行为并不等价，本文件按内核入口
 * 的语义写：
 *
 *   a) ot 的初值。C 是 if/else if/else 选**一个** ri，汇编是两个条件独立
 *      判断后**相加**：
 *          term_ri0 = (ri1 & ot_mask) == ot_tester2 ? 0 : ri0
 *          term_ri1 = (ri0 & ot_mask) == ot_tester1 ? 0 : ri1
 *          ij = (term_ri0 + term_ri1 + (ot==1 ? n_i : 0)) >> 1
 *      两个 tester 同时成立时 C 只取一个，汇编两个都留。
 *   b) C 的 `else` 兜底分支（ot==2 时那句 Schlendrian）在汇编里只对 ot==2
 *      存在，且触发条件也不同：汇编是 `b <= a`（取负掩码后的比较），
 *      C 写的是 `(ri0&mask) <= (ri1&mask)` —— 取负之后大小方向是反的。
 *   c) C 的兜底里用 u64 算 `ri1 - ri0`，汇编是 32 位 `subl` 后 `shrl`，
 *      会回绕。
 *   d) C 那条 `if (i < b) ij += ri[0]` 本身是对的（见第 2 条），没有分歧。
 */

#include <stdint.h>

#include "siever-config.h"

#ifndef I_bits
#error "I_bits 必须由 Makefile 的 -DI_bits=<11..16> 传入（与汇编的 -Dn_i_bits=I-1 对应）"
#endif

/* L1_BITS 在 siever-config.h 里固定为 15。 */
#include "lasieve_ns.h"

namespace lasieve_ns {
static_assert(L1_BITS == 15, "siever-config.h 的 L1_BITS 固定为 15");
static_assert(I_bits >= 2 && I_bits <= 16, "I_bits 超出 per-I 库的范围");

#define LS_L1_BITS   L1_BITS
#define LS_L1_SIZE   (1u << LS_L1_BITS)
#define LS_L1_MASK   (LS_L1_SIZE - 1u)
#define LS_NI_BITS   (I_bits - 1)
#define LS_NI        (1u << LS_NI_BITS)
#define LS_NI_MASK   (LS_NI - 1u)

/* 汇编里 `addl $65536,%r9d`，即 fbi 的增量是 1 << 16。 */
#define LS_FBI_INCR  (1u << 16)


u32_t *lasched0(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched0nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched1(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched1nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched2(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched2nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched3(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched3nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched0_1(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched1_1(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched1_1nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched2_1(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched2_1nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched3_1(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);
u32_t *lasched3_1nt(u32_t *, u32_t *, u32_t *, u32_t, u32_t **, u32_t);


/* ot_tester1 = (ot&1) | ((ot&2) << (n_i_bits-1))，ot_tester2 = n_i ^ ot_tester1。
 * 汇编里是 `eval(...)` 在 m4 展开期算出来的，ot=1/2/3 对应 1/1024/1025 与
 * 1025/0/1（n_i=1024 时），这里在编译期算，值完全一致。 */
#define LS_TESTER1(ot)   ((u32_t)(((ot) & 1) | (((ot) & 2) << (LS_NI_BITS - 1))))
#define LS_TESTER2(ot)   ((u32_t)(LS_NI ^ LS_TESTER1(ot)))
#define LS_OT_MASK       ((u32_t)(LS_NI | 1u))

/* OT == 0 是 lasched0.asm 的形状：不做 tester 选择，ij 直接取自 ij_ptr。
 * OT >= 1 是 lasched1.asm 的形状。U16 对应 lasched*_1（16 位存储）。 */
template <int OT, bool U16>
static u32_t *
lasched_core(u32_t *ri, u32_t *ij_ptr, u32_t *ij_ptr_ub, u32_t n1_j,
             u32_t **sched_ptr, u32_t fbi_offs)
{
	/* `shlq $n_i_bits,%rcx` 是 64 位移位，但内层只用到低 32 位（见第 9 条）。 */
	const u32_t ij_ub = (u32_t)((u64_t)n1_j << LS_NI_BITS);
	/* 回边那条 `cmpq %rsi,(%rsp)` 比的是第三个参数减 4 **字节**（一个元素）。 */
	const uintptr_t back_edge_ub = (uintptr_t)((unsigned char *)ij_ptr_ub - 4);

	u32_t ijd = (OT == 0) ? *ij_ptr : 0u;
	u32_t fbi;

	/* lasched*_1 那几份**完全不用 fbi_offs**：入口没有 `shrq $16`，回边没有
	 * `addl $65536`，内层也没有 `orl %r9d,%r11d`。所以 fbi 直接取 0，
	 * 存出去的 16 位就是纯的 ij & (L1_SIZE-1)。这一点与 kernels/lasched.c 里
	 * 那段注释一致：「we only store the 2-byte masked ij instead of the
	 * 32-bit OR'd value above」。 */
	fbi = U16 ? 0u : (fbi_offs >> 16);

	/* `cmpq %rsi,%rdx ; jbe`：ij_ptr >= ij_ptr_ub 就一次都不跑。 */
	if ((uintptr_t)ij_ptr >= (uintptr_t)ij_ptr_ub)
		return ri;

	for (;;) {
		u32_t *const cur = ij_ptr;   /* 回边的 cmp/jb 用的是自增前的 ij_ptr */
		const u32_t ad = ri[0];
		const u32_t bd = ri[1];
		/* ad/bd 的原值另存一份给内层的 cmov 用（汇编的 v0=%edx / v1=%ebp）。 */
		const u32_t v0 = ad;
		const u32_t v1 = bd;
		u32_t a, b;
		u32_t i0, i1;

		if (OT >= 1) {
			/* 汇编里是两个条件**独立**判断后相加，cmovne 走的是「不成立」
			 * 的那一支 —— 所以成立时那一项取 0，不成立时取对方那条线的 ri。
			 * 这一段每轮都重算，所以回边算出来的 ijd 传不下去。 */
			const u32_t match_ad = ((ad & LS_OT_MASK) == LS_TESTER1(OT));
			const u32_t match_bd = ((bd & LS_OT_MASK) == LS_TESTER2(OT));
			u64_t j;

			j  = (u64_t)(match_bd ? 0u : ad);
			j += (u64_t)(match_ad ? 0u : bd);
			if (OT == 1)
				j += LS_NI;               /* `ifelse(ot,1,`addq $n_i,ij')` */
			ijd = (u32_t)(j >> 1);       /* `shrq $1,%rax`，64 位逻辑右移 */
		} else {
			/* lasched0.asm 的回边是 `movl 4(%rsi),%eax` —— 从**输入数组**
			 * 里重新读下一个元素当起始 ij，而不是接着用上一轮算出来的 ijd。
			 * 所以 ij_ptr[k] 写回去的 (ij - ij_ub) **不会**传给第 k+1 轮；
			 * 每轮的起始 ij 各自来自 ij_ptr[k]。写成沿用上一轮的 ijd 就错了
			 * （n == 1 时看不出来，n >= 2 才暴露）。
			 * lasched1.asm 则相反：回边不重读 ij_ptr[1]，ijd 留在寄存器里，
			 * 但下一轮开头就被上面的 ri 推导整个覆盖掉，所以那边等价。 */
			ijd = *cur;
		}

		/* `negl` 之后 `andl $n_i_mask`，注意 0 取负掩码后是 0 而不是 n_i。 */
		a = ((u32_t)(0u - ad)) & LS_NI_MASK;
		b = ((u32_t)(0u - bd)) & LS_NI_MASK;

		if (OT == 2) {
			/* `cmpl %r14d,%r15d ; jbe` → b CMP a → b <= a 时进异常块。
			 * 这一步发生在 shrq $1 之后，所以 ij 保留上面算出的值。 */
			if (b <= a) {
				/* `cmpl %ebx,4(%rdi) ; jb` → bd CMP ad → bd < ad 时原样跳过。 */
				if (bd >= ad) {
					if (b == a) {
						/* 这里是 `movl $n_i,%eax` 之后紧跟
						 * `cmovel 4(ri),%eax` —— 后者是**覆盖**不是相加，
						 * 所以 a == b 时 n_i 那个初值根本没留下，结果是
						 * (ri[1] - ri[0]) >> 1。差一位就变成
						 * (n_i + ri[1] - ri[0]) >> 1，差 n_i/2。
						 * 32 位减法会回绕，上面 bd >= ad 那道判断就是为了
						 * 避开回绕。 */
						ijd = (u32_t)(bd - ad) >> 1;
					} else {
						/* cmovel 不成立，eax 保留 movl $n_i 那个值。 */
						ijd = LS_NI >> 1;
					}
				}
			}
		}

		/* aux0d / aux1d：每轮内层循环都从当前 ijd 重新取。 */
		i0 = ijd;
		i1 = ijd;

		/* `cmpl %eax,%ecx ; jbe` → ij_ub <= ijd 就跳过整段内层。 */
		while (ij_ub > ijd) {
			const u32_t i  = i0 & LS_NI_MASK;
			const u32_t sx = i1 >> LS_L1_BITS;
			/* movl/andl/orl 三步都在 ijd 被加之前完成。 */
			const u32_t d  = (ijd & LS_L1_MASK) | fbi;
			const u32_t d3 = (a <= i) ? v1 : 0;   /* cmpl %ebx,%r14d ; cmovbe */
			const u32_t d4 = (b >  i) ? v0 : 0;   /* cmpq %rbx,%r15  ; cmova  */
			u32_t *p = sched_ptr[sx];

			ijd += d3;
			ijd += d4;

			if (U16) {
				/* `movw %r11w,(%rbx)` + `leaq 2(%rbx)`：存低 16 位、指针 +2 字节。 */
				*(u16_t *)p = (u16_t)d;
				p = (u32_t *)((unsigned char *)p + 2);
			} else {
				*p = d;
				++p;
			}
			sched_ptr[sx] = p;

			i0 = ijd;
			i1 = ijd;
		}

		/* `subl %ecx,%eax ; movl %eax,(%rsi)` —— 32 位减法，会回绕。 */
		ijd -= ij_ub;
		*cur = ijd;

		if (!U16)
			fbi += LS_FBI_INCR;        /* lasched*_1 没有这一条 */

		/* `leaq 8(ri),%ri` 与 `leaq 4(ij_ptr),%ij_ptr` 都在 ja **之前**，
		 * 所以 ri 每轮无条件前进 2 个元素，**包括最后一轮**。回边的
		 * `cmpq %rsi,(%rsp)` 比的是自增之前的 ij_ptr。 */
		ri += 2;
		ij_ptr = cur + 1;

		/* 回边：继续的条件是 自增前的 ij_ptr < (ij_ptr_ub - 4 字节)。 */
		if ((uintptr_t)cur >= back_edge_ub)
			break;
	}

	return ri;
}

#define LS_DEF(name, OT, U16)                                                  \
	u32_t *name(u32_t *ri, u32_t *ij_ptr, u32_t *ij_ptr_ub,         \
	                       u32_t n1_j, u32_t **sched_ptr, u32_t fbi_offs)       \
	{                                                                          \
		return lasched_core<OT, U16>(ri, ij_ptr, ij_ptr_ub, n1_j, sched_ptr,  \
		                             fbi_offs);                                \
	}

/* 32 位存储版本（`movl aux2d,(%r0)`，fbi_offs 先右移 16、每轮 +65536） */
LS_DEF(lasched0,     0, false)
LS_DEF(lasched1,     1, false)
LS_DEF(lasched2,     2, false)
LS_DEF(lasched3,     3, false)
/* *nt 只是把存储换成 `movnti`，语义与上面完全相同，本文件不区分。 */
LS_DEF(lasched0nt,   0, false)
LS_DEF(lasched1nt,   1, false)
LS_DEF(lasched2nt,   2, false)
LS_DEF(lasched3nt,   3, false)

/* 16 位存储版本（`movw aux2w,(%r0)`，指针 +2，fbi_offs 不右移也不递增） */
LS_DEF(lasched0_1,   0, true)
LS_DEF(lasched1_1,   1, true)
LS_DEF(lasched2_1,   2, true)
LS_DEF(lasched3_1,   3, true)
LS_DEF(lasched1_1nt, 1, true)
LS_DEF(lasched2_1nt, 2, true)
LS_DEF(lasched3_1nt, 3, true)

#undef LS_DEF
}  /* namespace lasieve_ns */
