/* medsched —— 由 factor/nfs/lasieve/asm/medsched0.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「中筛排线」内核：和 lasched 同一条筛线递推，但排线缓冲不是按
 * `ij >> L1_BITS` 分桶的 u32 数组，而是**一条连续的 u32 指针**（sched_ptr
 * 指向的那个位置本身就是当前写头）。每命中一次就 `movl %r9d,(%r12)` 写一个
 * 32 位字、指针 +4，然后把 ij 减去 L1_SIZE 写回 ij_ptr 传下去。
 *
 * 两个入口（注意第二个的符号名是 medsched0_1，不是 medsched_1）：
 *   medsched0   (ri, ij_ptr, ij_ptr_ub, sched_ptr, fbi_offs)
 *   medsched0_1 (ri, ij_ptr, ij_ptr_ub, si, lo)
 * 前者往排线缓冲写 (fbi_offs<<16)|ij，后者往字节数组 si 里做 si[ij] += lo。
 * asm/medsched.h 里声明的 medsched / medsched_1 是 C 层的分发入口，本文件
 * 只提供它俩要转调的这两个汇编级内核（medsched.c 里也是这么声明的）。
 *
 * asm/medsched1.asm / medsched2.asm 等本仓库里不存在；medsched 的 ot!=0
 * 变体在 C 版里是标量/AVX512 实现，没有对应的汇编内核，所以这里只有 0。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) 写头是**从 sched_ptr 指向的位置取出来、最后再写回去**的：
 *      movq (%rcx),%r12          ; 入口就取，即使一次都不跑也要取
 *      movq %r12,(%rcx)          ; medsched_fbiloop_end 处无条件写回
 *    所以「n==0 时循环不跑」不等于「sched_ptr 不变」—— 它被读出来再原样写回，
 *    值不变，但差分测试仍要比较 *sched_ptr。
 *
 * 2) 入口判的是未减 4 的 %rdx，回边判的是 `leaq -4(%rdx),%rdx` 之后的值
 *    （4 **字节** = 1 个 u32 元素）。继续的条件是 `ij_ptr < ij_ptr_ub - 1 元素`，
 *    也就是这一轮正好处理 (arg3-arg2)/4 个元素。和 lasched 同一个坑。
 *
 * 3) 存进排线缓冲的是**完整 32 位**：`orl %r8d,%r9d` 在前、`movl %r9d,(%r12)`
 *    紧随其后，而 `andl $32767,%r9d` 在存储**之后**。所以写出去的是
 *    (fbi_offs<<16) | ij，不是 lasched 那种「低 16 位放 fbi_offs」——
 *    medsched 把 ij 整条放进低 16 位以外… 准确说：ij < 2^15 占 bit 0..14，
 *    fbi_offs<<16 占 bit 16..31，bit 15 恒为 0。
 *
 * 4) fbi_offs 是 `shll $16,%r8d` 入口左移、回边 `addl $65536,%r8d` 递增。
 *    和 lasched 相反（那边是右移），等价于 C 的 fbi_offs << 16 加每轮 ++，
 *    前提 fbi_offs < 2^16。
 *
 * 5) medsched0_1 里**完全没有 fbi_offs**：没有 orl、没有 addl $65536，
 *    参数表第三个之后是 si(rcx) 和 lo(r8b)。存储是 `addb lo,(si,ijq,1)`，
 *    8 位回绕加法，且用的 ij 是**加法之前**的值。
 *
 * 6) 两个 cmov 的方向同 lasched（AT&T，op src,dst）：
 *      cmpl %r10d,%eax ; cmovbel → a CMP i，a <= i → 加 ri[1]
 *      cmpl %r10d,%ebx ; cmoval  → b CMP i，b >  i → 加 ri[0]
 *    注意中间的 `orl %r8d,%r9d` / `andl $32767,%r9d` 会改标志位，所以 `cmoval`
 *    必须在它之后 —— 现有顺序是对的，不要重排。
 *
 * 7) a / b 同样是 `negl` 再 `andl $n_i_mask`：`(-ri[x]) & mask`。写成
 *    n_i - (ri[x] & mask) 会在 ri[x]&mask==0 时差 1。
 *
 * 8) 内层循环的出口判 `ij < L1_SIZE`（`cmpl $32768,%r9d ; jb`），入口由
 *    `jae ..._ij_loop_end` 保证至少跑一轮，所以是 do/while 形状。
 *    ij 是 32 位，`addl` 会回绕。
 *
 * 9) ri 的两个分量是「交错」存放的：`(ri)` 是 ri[2k]、`4(ri)` 是 ri[2k+1]，
 *    回边 `leaq 8(ri),ri` 前进 2 个元素。这对应 C 参考里
 *    `RI_OFFSET1 = 1; RI_INCR = 2;`（CONTIGUOUS_RI 在本仓库里没有定义）。
 */

#include <stdint.h>

#include "siever-config.h"

#ifndef I_bits
#error "I_bits 必须由 Makefile 的 -DI_bits=<11..16> 传入（与汇编的 -Dn_i_bits=I-1 对应）"
#endif

#include "lasieve_ns.h"

namespace lasieve_ns {
static_assert(L1_BITS == 15, "ls-defs.asm 的 l1_bits 固定为 15");
static_assert(I_bits >= 2 && I_bits <= 16, "I_bits 超出 per-I 库的范围");

#define MS_L1_SIZE   (1u << L1_BITS)
#define MS_NI_BITS   (I_bits - 1)
#define MS_NI_MASK   ((1u << MS_NI_BITS) - 1u)
#define MS_FBI_INCR  (1u << 16)


/* 声明照抄 asm/medsched.c 里的那两行（medsched.h 里只有 C 层的 medsched /
 * medsched_1，没有这两个）。 */
u32_t *medsched0(u32_t *, u32_t *, u32_t *, u32_t **, u32_t);
u32_t *medsched0_1(u32_t *, u32_t *, u32_t *, unsigned char *, unsigned char);


u32_t *
medsched0(u32_t *ri, u32_t *ij_ptr, u32_t *ij_ptr_ub, u32_t **sched_ptr,
          u32_t fbi_offs)
{
	/* `movq (%rcx),%r12`：入口无条件取写头，末尾无条件写回。 */
	u32_t *const sched_head = (u32_t *)(uintptr_t)*sched_ptr;
	u32_t *sched = sched_head;
	const uintptr_t back_edge_ub = (uintptr_t)((unsigned char *)ij_ptr_ub - 4);

	/* `shll $16,%r8d`：medsched 是左移，lasched 是右移。 */
	u32_t fbi = fbi_offs << 16;

	/* `cmpq %rsi,%rdx ; jbe` */
	if ((uintptr_t)ij_ptr >= (uintptr_t)ij_ptr_ub) {
		*sched_ptr = (u32_t *)(uintptr_t)sched;
		return ri;
	}

	for (;;) {
		u32_t *const cur = ij_ptr;   /* 回边的 cmp/ja 用的是自增前的 ij_ptr */
		u32_t ij = *cur;
		const u32_t ad = ri[0];
		const u32_t bd = ri[1];

		/* `cmpl $32768,%r9d ; jae`：出口已保证的轮次才进内层。 */
		if (ij < MS_L1_SIZE) {
			/* `negl` + `andl $n_i_mask`，0 取负掩码后是 0 不是 n_i。 */
			const u32_t a = ((u32_t)(0u - ad)) & MS_NI_MASK;
			const u32_t b = ((u32_t)(0u - bd)) & MS_NI_MASK;
			u32_t i1 = ij;               /* aux1 */

			do {
				const u32_t i  = i1 & MS_NI_MASK;
				/* orl 在前、movl 在后、andl $32767 在存储之后。 */
				const u32_t d  = ij | fbi;
				const u32_t d3 = (a <= i) ? bd : 0;   /* cmpl %r10d,%eax ; cmovbe */
				const u32_t d4 = (b >  i) ? ad : 0;   /* cmpl %r10d,%ebx ; cmova  */

				*sched++ = d;

				ij += d3;
				ij += d4;
				i1 = ij;
			} while (ij < MS_L1_SIZE);   /* `cmpl $32768,%r9d ; jb` */
		}

		/* `subl $l1_size,%r9d` —— 32 位减法。medsched 的界是 L1_SIZE，
		 * 不是 lasched 的 n1_j << n_i_bits。 */
		ij -= MS_L1_SIZE;
		*cur = ij;

		fbi += MS_FBI_INCR;

		/* `leaq 8(ri),%ri` 与 `leaq 4(ij_ptr),%ij_ptr` 都在 ja **之前**，
		 * 所以 ri 每轮无条件前进 2 个元素，**包括最后一轮**；回边的
		 * `cmpq %rsi,%rdx` 比的是自增之前的 ij_ptr。 */
		ri += 2;
		ij_ptr = cur + 1;

		if ((uintptr_t)cur >= back_edge_ub)
			break;
	}

	*sched_ptr = (u32_t *)(uintptr_t)sched;
	return ri;
}

u32_t *
medsched0_1(u32_t *ri, u32_t *ij_ptr, u32_t *ij_ptr_ub, unsigned char *si,
            unsigned char lo)
{
	const uintptr_t back_edge_ub = (uintptr_t)((unsigned char *)ij_ptr_ub - 4);

	/* `cmpq %rsi,%rdx ; jbe` —— 这里没有 sched_ptr 要写回。 */
	if ((uintptr_t)ij_ptr >= (uintptr_t)ij_ptr_ub)
		return ri;

	for (;;) {
		u32_t *const cur = ij_ptr;   /* 回边的 cmp/ja 用的是自增前的 ij_ptr */
		u32_t ij = *cur;
		const u32_t ad = ri[0];
		const u32_t bd = ri[1];
		/* 这份把原值另存一份（ria/rib），medsched0 那份是直接重读内存。 */
		const u32_t ria = ad;
		const u32_t rib = bd;

		if (ij < MS_L1_SIZE) {
			const u32_t a = ((u32_t)(0u - ad)) & MS_NI_MASK;
			const u32_t b = ((u32_t)(0u - bd)) & MS_NI_MASK;
			u32_t i1 = ij;

			do {
				const u32_t i  = i1 & MS_NI_MASK;
				const u32_t d3 = (a <= i) ? rib : 0;
				const u32_t d4 = (b >  i) ? ria : 0;

				/* `addb lo,(si,ijq,1)`：8 位回绕，用的是加法前的 ij。 */
				si[ij] = (unsigned char)(si[ij] + lo);

				ij += d3;
				ij += d4;
				i1 = ij;
			} while (ij < MS_L1_SIZE);
		}

		ij -= MS_L1_SIZE;
		*cur = ij;

		/* 这份没有 `addl $65536,%r8d`。 */
		ri += 2;                        /* leaq 8(ri),%ri —— 在 ja 之前 */
		ij_ptr = cur + 1;               /* leaq 4(ij_ptr),%ij_ptr */

		if ((uintptr_t)cur >= back_edge_ub)
			break;
	}

	return ri;
}
}  /* namespace lasieve_ns */
