/* MMX-TD2 —— 由 MMX-TD2.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 两个入口，都是 MMX_Td.c 里 `MMX_REGW == 8` 那条分支调用的：
 *
 *   asm_MMX_Td8     试除：把命中的模数值（u16）追加到 pbuf（u32_t*），
 *                   返回推进后的 pbuf。
 *   asm_TdUpdate8   换直线后更新辅助缓冲区，原地写回。
 *
 * 声明在 MMX-TD.c:359 和 MMX-TD.c:420（不在任何头文件里），返回类型是
 * u32_t*，所以定义必须拿 C 链接 —— 下面自带 声明。
 *
 * ------------------------------------------------------------------
 * 照抄、不能「顺手改好」的地方。
 *
 * 1. `-80(aux_ptr,aux1q,2)` 这个负偏移不是笔误，它是软件流水的结果。
 *    入口一次 `leaq 48(aux_ptr),aux_ptr`，轮尾又一次，所以进 MMX_TdLoop1
 *    时 aux_ptr 已经指向「下一块」= 本块 + 96 字节。命中点要取的是
 *    本块的 x[8+id]（和 MMX-Td.c 里 `*(pbuf++) = x[MMX_REGW + id]` 一致），
 *    于是负偏移 96 - 80 = 16 字节 = 8 个 u16 正好回到 x[8]。照抄。
 *
 * 2. aux1(%r8) 是**累加**出来的，不是直接等于 id：
 *        bsfq ; shrl %cl,%esi ; shrq $1,%rcx ; addq %rcx,%aux1q ; 读 ; incl aux1
 *    每命中一次：aux1 += （剩余掩码里的最低位/2），然后 aux1++。
 *    因为掩码每轮右移了「已处理位数」，这个累加会望远镜式地收敛到
 *    「本块内的字下标 id」：命中序列 id0<id1<id2 时三轮里 aux1 依次是
 *    id0、id0+1+(id1-id0-1)=id1、…，读之前那次正好是 id。
 *    下面照抄这串指令，**不要**简化成「在未右移的掩码上 ctz 再 m>>=(id+2)」：
 *    `andl $0x5555` 之后置位的全是偶数位，右移两位之后最低位又落回 0，
 *    ctz 恒为 0，第二个命中点起全部读成 x[8+0]。差分测试抓得到这个。
 *
 * 3. `pmulhw` 是**有符号**高乘（_mm_mulhi_epi16）。MMX-Td.c 里那段
 *    C 参考用的是 _mm_mulhi_epu16 —— 两者返回的 16 位相同（32 位乘积的
 *    高半与有无符号无关），但和文件里那个纯标量 fallback
 *    `((modulo32 * (u32)t) & 0xffff0000) == 0` 在 t 或 modulo32 的
 *    第 15 位为 1 时结论可以不同。这里照抄汇编用的有符号形式。
 *
 * 4. 广播 strip_i 的方式是
 *        movl strip_i,%r8d ; shll $16,%r8d ; orl %r8d,%esi
 *        movd %rsi,%xmm3 ; pshufd $0,%xmm3,%xmm3
 *    即「把 32 位量 strip_i|(strip_i<<16) 的低 dword 广播到 4 个 dword」。
 *    strip_i 的**声明类型是 u32_t**（虽然 MMX_Td 传进来的是 u16_t 的
 *    strip_i），strip_i >= 0x10000 时低高两个字不一样，pshufd 广播出来的
 *    8 个字就不再均匀。写成 _mm_set1_epi32(strip_i | (strip_i<<16))
 *    才是逐位等价，不能图省事写 _mm_set1_epi16。
 *
 * 5. `movd %rsi,%xmm3` 被 GAS 编成带 REX.W 的 **movq**（反汇编里就是
 *    `movq xmm3,rsi`）。这里无害：orl 是 32 位操作，rsi 高 32 位必然是 0，
 *    而 pshufd $0 只看低 dword。但如果照抄成「movq 一个 64 位值再 pshufd」
 *    就得连带保证高 32 位为 0，写成 set1_epi32 就自动绕开了。
 *
 * 6. 循环是 do-while，但**出口判在轮中**：
 *        cmpq aux_ptr,aux_ptr_ub ; pmullw ; movdqa (aux_ptr),%xmm2 ; jbe MMX_TdEnde
 *    也就是退出判断之前已经把下一块的 [0..7] 读进来了。`cmpq a,b` 在
 *    AT&T 里算的是 b-a，反汇编是 `cmp rax,rdx`（rax=aux_ptr_ub、rdx=aux_ptr），
 *    所以 jbe 判的是「aux_ptr_ub <= aux_ptr」，含边界。照抄这个方向 ——
 *    写反了会让 n 很小的时候照样处理一块、越界读一大截。
 *
 * 7. `movd %xmm3,%r10d` 之后 %r10 再没被读过 —— 死代码，不搬。
 *
 * 8. 全部访存都是 `movdqa`，要求 **16 字节对齐**（MMX-Td.c 的
 *    mmx_xmalloc 也只保证 MMX_REGW*2 = 16 字节对齐）。C 参考版本用的是
 *    _mm_loadu_si128，这里用 _mm_load_si128 / _mm_store_si128 保住这个
 *    要求：不对齐时汇编是 #GP 崩掉，C++ 也应该崩，而不是悄悄跑出结果。
 *
 * 9. asm_MMX_Td8 的第 4 个实参在 .asm 里叫 aux_ptr_ub_arg(%rcx)，而
 *    `movq aux_ptr_ub_arg,%rax` 之后 `leaq 24(aux_ptr_ub),%rax` 把
 *    **+24 字节 = +12 个 u16** 加了进去才拿去比较。照抄。
 */

#include <stdint.h>

#include <emmintrin.h>

#include "siever-config.h"

/* MMX-TD.c:359 / MMX-TD.c:420 的声明。返回类型 u32_t* 与那边一致。 */

#include "lasieve_ns.h"

namespace lasieve_ns {
u32_t *asm_MMX_Td8(u32_t *pbuf, u32_t strip_i, u16_t *aux_ptr,
		   u16_t *aux_ptr_ub);

u32_t *asm_TdUpdate8(u16_t *auxptr, u16_t *auxptr_ub, u16_t *uptr);


/* ------------------------------------------------------------------
 * asm_MMX_Td8
 *
 * 布局：辅助缓冲区每块 3*8 = 24 个 u16 ——
 *     x[ 0.. 7] = 乘上 strip_i 的那一组
 *     x[ 8..15] = 被乘的第二组
 *     x[16..23] = 模数
 * 判据是「(x[8+i] * ((x[i]+strip_i) * x[16+i])) 的高 16 位为 0」，
 * 命中就把 x[8+i] 追加到 pbuf。
 *
 * 出口条件（无符号）：aux_ptr_ub <= ap 时停，lim = aux_ptr_ub + 12（leaq 24），
 * ap 从 aux_ptr + 24 起、每轮 +24，所以实际处理的是
 *     aux_ptr[0..23], aux_ptr[24..47], ...
 * 也就是 #{ k : 24k < n - 12 } 块，n <= 12 时一块都不处理。
 */
u32_t *asm_MMX_Td8(u32_t *pbuf, u32_t strip_i, u16_t *aux_ptr,
		   u16_t *aux_ptr_ub)
{
	const __m128i vs = _mm_set1_epi32((int)(strip_i | (strip_i << 16)));
	const __m128i zero = _mm_setzero_si128();

	u16_t *ap = aux_ptr;
	u16_t *const lim = aux_ptr_ub + 12;      /* leaq 24(aux_ptr_ub),%rax */

	/* 入口三条 movdqa + paddw 取的是第一块；入口的 leaq 把 ap 推到下一块。 */
	__m128i t = _mm_add_epi16(
		_mm_load_si128((const __m128i *)ap), vs);        /* paddw %xmm3,%xmm2 */
	__m128i x1 = _mm_load_si128((const __m128i *)(ap + 8)); /* movdqa 16 */
	__m128i x2 = _mm_load_si128((const __m128i *)(ap + 16));/* movdqa 32 */
	ap += 24;                                             /* leaq 48 */

	for (;;) {
		/* 轮首这三条算的是**上一块**的量（软件流水），轮尾三条 movdqa
		 * 预取下一块。顺序照抄，否则块和块的对应会整体错一格。 */
		const __m128i lo = _mm_mullo_epi16(x2, t);        /* pmullw %xmm2,%xmm1 */
		const __m128i n0 = _mm_load_si128((const __m128i *)ap);

		/* cmpq aux_ptr,aux_ptr_ub ; jbe —— 反汇编里是 `cmp rax,rdx`
		 * （rax=aux_ptr_ub、rdx=aux_ptr），算的是 aux_ptr_ub - aux_ptr，
		 * 所以 jbe 判的是「aux_ptr_ub <= aux_ptr」，含边界。
		 * 这一句排在预取**之后**：退出那一轮也会读一次 ap[0..7]。 */
		if ((uintptr_t)lim <= (uintptr_t)ap)
			break;

		const __m128i hi = _mm_mulhi_epi16(x1, lo);       /* pmulhw %xmm1,%xmm0 */
		const __m128i n2 = _mm_load_si128((const __m128i *)(ap + 16));
		const __m128i nt = _mm_add_epi16(n0, vs);         /* paddw %xmm3,%xmm2 */
		u32_t m = (u32_t)_mm_movemask_epi8(_mm_cmpeq_epi16(hi, zero))
			  & 0x5555u;                              /* pmovmskb + andl */
		const __m128i n1 = _mm_load_si128((const __m128i *)(ap + 8));

		ap += 24;                                         /* leaq 48(aux_ptr) */

		/* bsf 那串：aux1(%r8) 是**累加**出来的，不是直接等于 id。
		 *     andl $0x5555 之后置位的都是偶数位，所以 bsf 给的是 2*id，
		 *     shrq $1 之后才是 id；每命中一次 aux1 += id、读完再 aux1++。
		 *     因为掩码每轮已经右移过「已处理位数」，这个累加会望远镜式地
		 *     收敛到「本块内的字下标」：命中序列 id0<id1<id2 时三轮里
		 *     aux1 依次是 id0、id0+1+(id1-id0-1)=id1、…，读之前那次正好
		 *     是 id。下面照抄指令顺序，不做等价化简。 */
		{
			size_t aux1 = 0;

			while (m) {
				const unsigned b =
					(unsigned)__builtin_ctz(m);   /* bsfq */

				m >>= b;                              /* shrl %cl,%esi */
				aux1 += (size_t)(b >> 1);             /* shrq $1 / addq */

				/* 汇编：movzwl -80(aux_ptr,aux1q,2),%r9d。
				 * 此时 ap 已 +24，入口那次 +24 也在里面，
				 * ap = 本块 + 96 字节；-80 + 2*aux1 正好落回本块
				 * 的 x[8+id]。movl 存的是零扩展后的 u16。
				 * 偏移是**字节**，所以在 unsigned char* 上算。 */
				pbuf[0] = *(const u16_t *)
					((const unsigned char *)ap
					 + (-80 + 2 * (ptrdiff_t)aux1));

				m >>= 2;                              /* shrl $2,%esi */
				pbuf++;
				aux1++;                               /* incl aux1 */
			}
		}

		x1 = n1;
		x2 = n2;
		t = nt;
	}

	return pbuf;
}

/* ------------------------------------------------------------------
 * asm_TdUpdate8
 *
 * `cmpq auxptr,auxptr_ub_arg ; jbe` 是「auxptr >= auxptr_ub 就整段跳过」，
 * 之后是一个 do-while，条件是 `ja`：`cmpq auxptr,auxptr_ub` 里
 * auxptr_ub 已经 `leaq -48` 退过 48 字节 = 24 个 u16，所以「再转一圈」的
 * 条件是「auxptr_ub - 24 个 u16 > 本块起点」。**这条 cmp 排在轮首**，
 * 在轮尾那条 `leaq 48(auxptr),auxptr` 之前，所以判定用的是本块起点而不是
 * 推进后的位置 —— 写成 `while (ap < lim)`（ap 已 +24）会整整少跑一轮，
 * n=25 就能看出来（汇编跑 2 轮）。轮数因此是
 *     iters(n) = 1 + #{ m >= 0 : 24m < n - 24 } = (n > 0 ? ceil(n/24) : 0)
 * 照抄。
 *
 * 每轮：p = 本块 x[8..15]，x = 本块 x[0..7]，y = 下一条直线的射影根；
 *       x = (y > x) ? (x - y + p) : (x - y)，全部按 2^16 回绕。
 * ap 每次 +24 个 u16（3*MMX_REGW），y 每次 +8 个 u16（MMX_REGW）。
 *
 * 注意 `pcmpgtw` 是**有符号**16 位比较，而 MMX-Td.c 里那段 C 参考用的是
 * _mm_cmpgt_epu16_mask（无符号）。x/y 里出现 >= 0x8000 的值时两者结论不同，
 * 这里照抄汇编的有符号形式。
 *
 * 返回值：汇编没有 ret 一个值，%rax 里留的是循环结束后的 auxptr。这里
 * 照着写，调用点 MMX_TdUpdate() 本来就丢弃返回值。
 */
u32_t *asm_TdUpdate8(u16_t *auxptr, u16_t *auxptr_ub, u16_t *uptr)
{
	if ((uintptr_t)auxptr >= (uintptr_t)auxptr_ub)    /* cmpq ; jbe */
		return (u32_t *)auxptr;

	u16_t *ap = auxptr;
	u16_t *const lim = auxptr_ub - 24;                /* leaq -48(auxptr_ub) */

	__m128i x = _mm_load_si128((const __m128i *)(ap + 0));   /* movdqa (%rax) */
	__m128i p = _mm_load_si128((const __m128i *)(ap + 8));   /* movdqa 0x10(%rax) */
	__m128i y = _mm_load_si128((const __m128i *)uptr);       /* movdqa (%r8) */
	uptr += 8;                                              /* leaq 16(%r8) */

	do {
		/* 汇编把 `cmpq auxptr,auxptr_ub` 放在轮**首**，在轮尾那条
		 * `leaq 48(auxptr),auxptr` **之前**，所以那一刻 rax 还是本块起点。
		 * 反汇编是 `cmp rcx,rax`（rcx = auxptr_ub-48、rax = 本块起点），
		 * `ja` 判的是「auxptr_ub-48 > 本块起点」，无符号。
		 * 「要不要再转一圈」必须在 ap 前进之前取，写在循环条件里就迟了一步、
		 * 少跑一轮（n=25 就能看出来：汇编跑 2 轮）。 */
		const bool again = (uintptr_t)lim > (uintptr_t)ap;

		const __m128i m = _mm_cmpgt_epi16(y, x);    /* pcmpgtw %xmm2,%xmm3 */
		const __m128i d = _mm_sub_epi16(x, y);      /* psubw %xmm0,%xmm2 */

		y = _mm_load_si128((const __m128i *)uptr); /* movdqa (%r8) */
		uptr += 8;                                 /* leaq 16(%r8) */

		const __m128i r =
			_mm_add_epi16(_mm_and_si128(m, p), d);  /* pand ; paddw */

		x = _mm_load_si128((const __m128i *)(ap + 24)); /* movdqa 48(auxptr) */
		p = _mm_load_si128((const __m128i *)(ap + 32)); /* movdqa 64(auxptr) */

		_mm_store_si128((__m128i *)ap, r);              /* movdqa %xmm3,(auxptr) */
		ap += 24;                                       /* leaq 48(auxptr) */

		if (!again)
			break;
	} while (1);

	return (u32_t *)ap;
}
}  /* namespace lasieve_ns */
