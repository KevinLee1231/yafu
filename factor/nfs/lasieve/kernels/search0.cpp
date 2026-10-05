/* (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 候选搜索（candidate search）内核。签名（照抄 kernels/include/siever-config.h）：
 *     u32_t lasieve_search0(unsigned char *sieve_interval,
 *                           unsigned char *horizontal_sievesums,
 *                           unsigned char *horizontal_sievesums_ub,
 *                           unsigned char *srb, unsigned char *srb_ub,
 *                           u16_t *cand, unsigned char *fss_sv);
 * 返回写进 fss_sv 的字节数（= ncand）。调用方随后做
 *     fss_sv[i] += horizontal_sievesums[cand[i] >> i_bits];
 * 所以本函数写进 fss_sv 的是**原始**筛法字节，不含横向和。
 *
 * ============================ 契约（差分测试必须满足） ============================
 * 1) `movdqa (%r10),%xmm0` 是**对齐**加载。si_ptr 恒为 sieve_interval + 64 的倍数，
 *    所以 **sieve_interval 必须 16 字节对齐**。不对齐汇编直接 #GP。
 * 2) 三个内层循环全是 **do-while**（比较只在循环体尾部，没有入口预检查）：
 *      - srb：`cmpq %r11,%r8 ; ja`  → srb_ub > srb
 *      - j  ：`cmpq %rsi,%rdx ; ja` → hzs_ub > hzs
 *      - i  ：`cmpq %r10,%r15 ; ja` → si_ptr_ub > si_ptr
 *    所以三个上界都至少要能走一轮。调用方给的
 *      hzs_ub - hzs = j_per_strip ≥ 1、srb_ub - srb = n_i/128 ≥ 1。
 * 3) cand / fss_sv 由调用方提供，函数不查容量。最大输出 =
 *    (n_i/128) * j_per_strip * 128 = L1_SIZE 个候选。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 4) 阈值 th 的广播。汇编只有三条：
 *        movd %rcx,%xmm7 ; punpcklbw ; punpcklwd ; pshufd $0
 *    其中 `%rcx` 里的 th 只占低 1 字节，movd 编出来是 movq（高位清零）。
 *    逐级展开后得到的是 **16 字节均匀广播**（已实测确认）：
 *        movq → [t,0,0,0,0,0,0,0 | 0×8]
 *        punpcklbw → [t,t,0,0,0,0,0,0 | 0×8]
 *        punpcklwd → [t,t,t,t,0,0,0,0 | 0×8]
 *        pshufd $0 → dword0 广播，而 dword0 已经是 t,t,t,t → 全 16 字节 t
 *    所以 `_mm_set1_epi8(th)` 与它**等价**。本文件仍照抄那三条 intrinsic，
 *    一来保持与汇编逐条对应，二来万一以后 m4 变了能立刻看出差异。
 *
 * 5) th = (u8)(st - (hzs + 1))，比 C 参考里的 `st1 = st - hzs` **小 1**。
 *    汇编多了一条 `incb %bl`（hzs_ptr[0] 先 +1 再参与减法），所以：
 *        选中位置 k  ⟺  si[k] > th  ⟺  si[k] >= st - hzs
 *    正好等于 C 参考的 `if (*i_o >= st1)`。**这个「减一」是差一位的典型坑**，
 *    写成 th = st - hzs 会让整条判据错开一位。
 *    判 `st <= hzs` 走快路径那条也是同一个 `incb`：
 *        subb %bl,%al ; jb store_many  ⟺  st < hzs+1  ⟺  st <= hzs
 *
 * 6) i 循环入口的快判 `cmpq $65535,%rbx ; jne search_sievereport`：
 *        xmm4 = max(th, x0, x1)   （x0=si[0..15], x1=si[16..31]）
 *        xmm5 = max(x2, x3)       （x2=si[32..47], x3=si[48..63]）
 *        xmm5 = max(xmm5, xmm4)   → 逐 lane 取 64 个字节与 th 的最大值
 *        pcmpeqb th → lane 全 1 ⟺ 该 lane 四个字节都 ≤ th
 *    movemask == 0xffff 即 **64 个字节全部 ≤ th**，整块没得报，直接跳
 *    i_loop_entry2。注意 xmm5 的初值是 x2（**没有**先与 th 取 max），
 *    th 是靠最后那条 `pmaxub %xmm4,%xmm5` 引进来的；写错这条会让
 *    「整块跳过」的条件变松或变紧。
 *
 * 7) 16 位掩码的拼装顺序（`salq` 的目标立即数）：
 *        pmovmskb %xmm0,%rax   (bit  0..15)
 *        pmovmskb %xmm2,%rcx ; salq $32   (bit 32..47)
 *        pmovmskb %xmm1,%rbx ; salq $16   (bit 16..31)
 *        pmovmskb %xmm3,%rcx ; salq $48   (bit 48..63)
 *    即 **bit k 对应第 k 个字节**（xmm0=0..15, xmm1=16..31, xmm2=32..47,
 *    xmm3=48..63）。整体 `xorq $-1` 取反得到「该报」的位。
 *    三条 salq 的立即数（32/16/48）不能互换。
 *
 * 8) 逐位扫描是经典 set-bit 惯用法，**顺序不能换**：
 *        bsfq %rax,%rcx ; addq %rcx,%rbx ; addq %rbx,%r10 ; shrq %cl,%rax
 *        movw %r10w,(%r9) ; movb (%rdi,%r10),%cl ; subq %rbx,%r10 ; incq %rbx
 *        shrq $1,%rax ; testq ; jnz
 *    净效果 auxreg 每轮 += (bsf 结果 + 1)，si 每轮先加上再减去 auxreg，
 *    于是 si 始终等于「块首 + 已消耗的位数」。其中 `shrq %cl,%rax` 用的是
 *    bsfq 结果的**低 8 位**（bsf < 64，安全），而紧接着的 `movb ...,%cl`
 *    会覆盖 %cl —— 两条 shift 的先后是必须的，不能重排。
 *
 * 9) search_sievereport 里 si_ptr 先 `subq %rdi,%r10` 变成**索引**（不是指针），
 *     末尾再 `leaq 64(%r10),%r10 ; leaq (%rdi,%r10),%r10` 变回指针。
 *     `movw %r10w,(%r9)` 存的是索引的**低 16 位**（cand 是 u16_t*，
 *     与 C 参考的 `cand[ncand] = i_o - sieve_interval` 同款截断）。
 *
 * 10) 三层循环的指针推进有个容易看漏的不变量：i 循环退出时
 *     **si_ptr 恰好等于 si_ptr_ub**（步长 64、判据 `si_ptr_ub > si_ptr`，
 *     起点差 128）。i_loop_ende 处
 *        si_ptr_ub += n_i ; si_ptr += n_i - 128
 *     正好让两者之差重新变回 128。所以窗口宽度恒为 CANDIDATE_SEARCH_STEPS，
 *     覆盖范围是 [j*n_i + i, +128)，与 C 参考的
 *     `i_o = sieve_interval + (j<<i_bits) + i` 一一对应。
 *     store_many_sievereports 的 many_sr_loop 步长 16、同样以 si_ptr==si_ptr_ub
 *     收尾，不变量一致 —— 两条路径都跳到同一个 i_loop_ende。
 *
 * 11) store_many_sievereports 的下标向量：
 *        rbx = (idx<<16) + idx + 0x00010000   → lane[0,1] = [idx, idx+1]
 *     每条 `addq $0x00020002` 把两个 lane 各 +2；`movd` 只取低 32 位，
 *     所以 lane[2,3] / lane[4,5] / lane[6,7] 由 psllq $32 + por 拼出
 *     [idx+2,idx+3] / [idx+4,idx+5] / [idx+6,idx+7]（已实测确认）。
 *     `movd %rax,%xmm3` 编成 32 位（`mov %eax,%xmm3`），而 `movd %rbx,%xmm0`
 *     编成 64 位；两者对本函数取值等价，搬运统一用 64 位形式。
 *
 * 12) paddw 的增量来自 `movq $0x00080008,%rax` 经 por 展开，实测是
 *     lane [8,8,8,8,0,0,0,0]（**不是** 8 个 lane 都 +8）。但存回时只用
 *     movq（低 64 位 = 前 4 个 lane），后 4 个 lane 从不被存也不被读，
 *     所以这个差别不影响结果；仍按原样复现。
 *
 * 13) `emms` 是 MMX 状态复位，本内核一条 MMX 指令都没用，删掉不影响语义。
 */

#include <stdint.h>
#include <emmintrin.h>
#include <x86intrin.h>

#include "siever-config.h"

#ifndef I_bits
#error "I_bits 必须由 Makefile 的 -DI_bits=<11..16> 传入（与汇编的 -Dn_i_bits=I-1 对应）"
#endif

#include "lasieve_ns.h"

namespace lasieve_ns {
static_assert(L1_BITS == 15, "siever-config.h 的 L1_BITS 固定为 15");
static_assert(I_bits >= 2 && I_bits <= 16, "I_bits 超出 per-I 库的范围");

/* siever-config.h 没有 8 位无符号类型，这里按内核的字节语义本地定义一个。 */
typedef unsigned char u8;

#define SS_NI_BITS   (I_bits - 1)
#define SS_NI        ((u64_t)1 << SS_NI_BITS)
#define SS_JPS       (1 << (L1_BITS - SS_NI_BITS))
#define SS_CS_STEPS  128
#define SS_CS_2STEPS (2 * SS_CS_STEPS)


u32_t lasieve_search0(unsigned char *, unsigned char *, unsigned char *,
                      unsigned char *, unsigned char *, u16_t *, unsigned char *);


u32_t
lasieve_search0(unsigned char *sieve_interval, unsigned char *hzs_ptr,
                unsigned char *hzs_ptr_ub, unsigned char *srb_ptr_arg,
                unsigned char *srb_ptr_ub, u16_t *cand, unsigned char *fss_sv)
{
	unsigned char *si_ptr    = sieve_interval;
	unsigned char *si_ptr_ub = sieve_interval + SS_CS_STEPS;
	unsigned char *csv       = fss_sv;
	unsigned char *srb_ptr   = srb_ptr_arg;
	unsigned char *si_ptr1   = si_ptr;

	for (;;) {   /* ---- srb 循环（do-while） ---- */
		for (;;) {   /* ---- j 循环（do-while） ---- */
			const u8 st  = srb_ptr[0];
			const u8 hzs = hzs_ptr[0];
			hzs_ptr++;

			if (st <= hzs) {
				/* ---- store_many_sievereports ----
				 * 整段 128 字节全报：cand 写连续下标，
				 * fss_sv 写对应的 16 个原始字节。 */
				const u64_t idx =
				        (u64_t)((uintptr_t)si_ptr -
				                (uintptr_t)sieve_interval);

				u64_t rbx = (idx << 16) + idx + 0x00010000ULL;
				__m128i x0 = _mm_cvtsi64_si128((long long)rbx);

				rbx += 0x00020002ULL;
				__m128i t1 = _mm_cvtsi64_si128((long long)rbx);

				rbx += 0x00020002ULL;
				__m128i t2 = _mm_cvtsi64_si128((long long)rbx);

				rbx += 0x00020002ULL;

				/* lane[0..3] = [idx, idx+1, idx+2, idx+3] */
				x0 = _mm_or_si128(x0, _mm_slli_epi64(t1, 32));

				/* lane[0..3] = [idx+4, idx+5, idx+6, idx+7] */
				__m128i x1 = _mm_cvtsi64_si128((long long)rbx);
				x1 = _mm_or_si128(_mm_slli_epi64(x1, 32), t2);

				/* paddw 增量，见第 12 条 */
				__m128i x3 = _mm_cvtsi64_si128(0x00080008LL);
				x3 = _mm_or_si128(x3, _mm_slli_epi64(x3, 32));

				/* many_sr_loop：步长 16，退出时 si_ptr == si_ptr_ub。
				 * ★ x1 的加步位置必须照抄：汇编里 `paddw %xmm3,%xmm1`
				 * 在 `movq %xmm1,24(%r9)` **之前**，所以 cand[12..15]
				 * 存的是**已加步**的 x1（idx+12..idx+15）。
				 * 而 x0 的加步在 `movq %xmm0,16(%r9)` 之前，
				 * cand[8..11] 同样是已加步的值。
				 * 写成「先存后加」会让 cand[12..15] 和 [20..23]
				 * 退回 idx+4..7 / idx+12..15（实测正是这个现象）。 */
				do {
					const __m128i a = _mm_loadl_epi64(
					        (const __m128i *)(const void *)si_ptr);
					const __m128i b = _mm_loadl_epi64(
					        (const __m128i *)(const void *)(si_ptr + 8));

					/* movq %xmm0,(%r9) */
					_mm_storel_epi64((__m128i *)(void *)cand, x0);
					/* leaq 16(%r10),%r10 */
					si_ptr += 16;
					/* paddw %xmm3,%xmm0 */
					x0 = _mm_add_epi16(x0, x3);
					/* movq %xmm1,8(%r9) —— x1 尚未加步 */
					_mm_storel_epi64((__m128i *)(void *)(cand + 4),
					                 x1);
					/* cmpq %r10,%r15 */
					/* paddw %xmm3,%xmm1 —— 在下面的 24(%r9) 之前 */
					x1 = _mm_add_epi16(x1, x3);
					/* movq %xmm2,(%r14) */
					_mm_storel_epi64((__m128i *)(void *)csv, a);
					/* movq %xmm0,16(%r9) —— x0 已加步一次 */
					_mm_storel_epi64((__m128i *)(void *)(cand + 8),
					                 x0);
					/* paddw %xmm3,%xmm0 */
					x0 = _mm_add_epi16(x0, x3);
					/* movq %xmm4,8(%r14) */
					_mm_storel_epi64((__m128i *)(void *)(csv + 8), b);
					/* leaq 16(%r14),%r14 */
					csv += 16;
					/* movq %xmm1,24(%r9) —— x1 已加步 */
					_mm_storel_epi64((__m128i *)(void *)(cand + 12),
					                 x1);
					/* leaq 32(%r9),%r9 */
					cand += 16;
					/* paddw %xmm3,%xmm1 */
					x1 = _mm_add_epi16(x1, x3);
				} while ((uintptr_t)si_ptr_ub > (uintptr_t)si_ptr);

			} else {
				/* th 比 C 参考的 st1 小 1，见第 5 条。 */
				const u8 th = (u8)(st - (u8)(hzs + 1));

				/* 阈值广播，见第 4 条（等价于 _mm_set1_epi8(th)）。 */
				__m128i thv = _mm_cvtsi64_si128((long long)th);
				thv = _mm_unpacklo_epi8(thv, thv);
				thv = _mm_unpacklo_epi16(thv, thv);
				thv = _mm_shuffle_epi32(thv, 0x00);

				/* search_i_loop：步长 64，退出时 si_ptr == si_ptr_ub。 */
				for (;;) {
					__m128i x0 = _mm_load_si128(
					        (const __m128i *)(const void *)si_ptr);
					__m128i x1 = _mm_load_si128(
					        (const __m128i *)(const void *)(si_ptr + 16));
					__m128i x4 = thv;
					__m128i x2 = _mm_load_si128(
					        (const __m128i *)(const void *)(si_ptr + 32));
					x4 = _mm_max_epu8(x0, x4);
					__m128i x3 = _mm_load_si128(
					        (const __m128i *)(const void *)(si_ptr + 48));
					__m128i x5 = x2;
					x4 = _mm_max_epu8(x1, x4);
					x5 = _mm_max_epu8(x3, x5);
					si_ptr += 64;
					x5 = _mm_max_epu8(x4, x5);

					if (_mm_movemask_epi8(_mm_cmpeq_epi8(x5, thv)) !=
					    0xffff) {
						/* ---- search_sievereport ----
						 * 逐字节判定「si[k] > th」的位，见第 7 条。 */
						x0 = _mm_max_epu8(thv, x0);
						x1 = _mm_max_epu8(thv, x1);
						x2 = _mm_max_epu8(thv, x2);
						x3 = _mm_max_epu8(thv, x3);
						x0 = _mm_cmpeq_epi8(thv, x0);
						x1 = _mm_cmpeq_epi8(thv, x1);
						x2 = _mm_cmpeq_epi8(thv, x2);
						x3 = _mm_cmpeq_epi8(thv, x3);

						u64_t m =
						        (u64_t)(u32_t)_mm_movemask_epi8(x0);
						m |= (u64_t)(u32_t)_mm_movemask_epi8(x2) << 32;
						m |= (u64_t)(u32_t)_mm_movemask_epi8(x1) << 16;
						m |= (u64_t)(u32_t)_mm_movemask_epi8(x3) << 48;
						m = ~m;   /* xorq $-1 */

						si_ptr -= 64;
						/* 从这里起 si 是**索引**，不是指针（见第 9 条）。 */
						uint64_t si =
						        (uint64_t)((uintptr_t)si_ptr -
						                   (uintptr_t)sieve_interval);
						uint64_t auxreg = 0;

						/* loop1：set-bit 扫描，见第 8 条。 */
						do {
							const unsigned b =
							        (unsigned)__builtin_ctzll(m);
							auxreg += b;
							si += auxreg;
							m >>= (b & 63);

							cand[0] = (u16_t)si;
							cand++;
							const u8 v = sieve_interval[si];
							si -= auxreg;
							auxreg++;
							m >>= 1;

							*csv++ = v;
						} while (m != 0);

						/* `leaq 64(%r10),%r10 ; leaq (%rdi,%r10),%r10`
						 * —— 索引回退 64（块首）扫完后，**再加 64** 变回指针，
						 * 即 si_ptr = sieve_interval + 块首 + 64。
						 * 漏掉那个 +64 的话 si_ptr 原地不动，回边判
						 * `si_ptr_ub > si_ptr` 恒真，i 循环会反复重扫同一块
						 * （实测死循环）。 */
						si_ptr = sieve_interval + si + 64;
					}

					/* ---- i_loop_entry2 ----
					 * `cmpq %r10,%r15 ; ja search_i_loop`：这是 i 循环的
					 * **回边**，判 si_ptr_ub > si_ptr 才跳回循环头。
					 * search_sievereport 结束时 `jmp i_loop_entry2` 也落到这里。
					 * 所以这个判断必须在**循环内部**、每轮末尾做一次；
					 * 写成循环外的 if 就变成「只判一次」，si_ptr 会一直推进
					 * 冲出区间（实测会跑到第 11 轮而不是第 2 轮）。 */
				i_loop_entry2:
					if ((uintptr_t)si_ptr_ub <= (uintptr_t)si_ptr)
						break;   /* → i_loop_ende */
				}
			}   /* 结束 `else`（慢路径） */

			/* ---- i_loop_ende ---- 窗口宽度重新变回 128，见第 10 条。 */
			si_ptr_ub += SS_NI;
			si_ptr += SS_NI - SS_CS_STEPS;

			if ((uintptr_t)hzs_ptr_ub > (uintptr_t)hzs_ptr)
				continue;   /* ja search_j_loop */
			break;         /* 落到 cs_ret2l0 */
		}

		/* ---- cs_ret2l0 ---- hzs_ptr 复位，窗口挪到下一段。 */
		hzs_ptr = hzs_ptr_ub - SS_JPS;
		srb_ptr++;
		si_ptr = si_ptr1;
		si_ptr_ub = si_ptr1 + SS_CS_2STEPS;
		si_ptr = si_ptr1 + SS_CS_STEPS;
		si_ptr1 = si_ptr;

		if ((uintptr_t)srb_ptr_ub > (uintptr_t)srb_ptr)
			continue;   /* ja search_j_loop */
		break;         /* lss_ende */
	}

	/* `movq %r14,%rax ; subq 48(%rsp),%rax` —— 已写出的 fss_sv 字节数。 */
	return (u32_t)(uint64_t)(csv - fss_sv);
}
}  /* namespace lasieve_ns */
