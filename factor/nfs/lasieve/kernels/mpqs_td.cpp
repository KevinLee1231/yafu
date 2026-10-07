/* (Copyright (C) 2004 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 一个入口：u32_t asm_td(u16 *relptr, u16 minus, u64_t qx)
 *
 * MPQS 的 trial division。作用是把「筛出来的素因子列表」rels[5..4+nr] 原地
 * 展开成完整的约减序列，并返回约减后的多项式值。
 *
 * ---------------------------------------------------------------------------
 * 全局数组的布局（mpqs.cpp 里填的，照抄时必须对上）
 * ---------------------------------------------------------------------------
 *   mpqs_FB_inv_info[4j+0..1] = p_j          mpqs_FB_inv_info[4j+8..9]   = inv_j
 *   mpqs_FB_inv_info[4j+2..3] = p_{j+1}      mpqs_FB_inv_info[4j+10..11] = inv_{j+1}
 *   mpqs_FB_inv_info[4j+4..5] = p_{j+2}      mpqs_FB_inv_info[4j+12..13] = inv_{j+2}
 *   mpqs_FB_inv_info[4j+6..7] = p_{j+3}      mpqs_FB_inv_info[4j+14..15] = inv_{j+3}
 *   mpqs_FB_start[2j] = s1_j                 mpqs_FB_start[2j+1]        = s2_j
 *   mpqs_FB[2j] = p_j                        mpqs_FB_inv[j]              = p_j^{-1}
 * 即 16 字节的 FB_inv_info 块对应 4 个素数，16 字节的 FB_start 块同样对应 4 个。
 * 汇编每轮各推进 32 / 16 字节，所以一轮吃 4 个素数，轮数 = td_begin / 4。
 *
 * FB 下标 0（mpqs_FB[0]）是占位素数，值为 0：FB_inv_info[0],[1] 和 [8],[9]
 * 从来没人写，所以 mpqs.cpp 构造时只从下标 2 开始填。第 0 轮因此要把它屏蔽掉
 * （见下面 0xfff0 那段注释）。
 *
 * ---------------------------------------------------------------------------
 * 看起来像笔误、但不能改的语义
 * ---------------------------------------------------------------------------
 * 1) 约减判据是 hi16(p * lo16((p - s + ind) * inv)) == 0，注意它不是 mpqs.cpp
 *    里 #else 分支那条 C 参考的 (p-s+ind)*inv 高 16 位是否为零。参考 C 走的是
 *    32 位乘法、汇编先 pmullw 截断到 16 位再 pmulhuw，两者边界行为不同，
 *    这里以汇编为准。三处都是模 2^16 回绕（psubw / paddw / pmullw）。
 *
 * 2) pmulhuw 是无符号高乘，必须 _mm_mulhi_epu16，不能用 _mm_mulhi_epi16。
 *
 * 3) pmovmskb 一个 16 位 lane 贡献两位（两个字节的 MSB 相同），lane k -> 第 2k、
 *    2k+1 位。于是「素数 m 的两个 residue」占第 4m..4m+3 位；把 d 与 (d>>2)
 *    或起来就得到每个素数一位的合并位，位置分别是 1、5、9、13。
 *
 * 4) 位提取靠 `shrl $n` 的进位，而 `shrl $n` 的 CF 是**第 n 位**（不是移位结果
 *    的第 0 位）。汇编里两处都写成「先 shr 再 adcq」，所以照抄时必须写成
 *    `cf = (x >> n) & 1; x >>= n;`。写成 `cf = x & 1; x >>= n;` 第二次以后就
 *    全错位（第一次因为已经先移过所以碰巧对）。
 *
 *    合并位的位置：d 与 (d>>2) 或起来之后，素数 m（lane 2m/2m+1）的合并位落在
 *    第 4m+1 位。
 *      第 0 轮：shrl $5、$4、$4 → CF 取 e[5]、e[9]、e[13] → FB 下标 1、2、3。
 *      循环体：shrl $1、$4、$4、$4 → CF 取 e[1]、e[5]、e[9]、e[13]，
 *              对应本轮的 FB base+0..3；移位后这些位落在 0、4、8、12
 *              （汇编里 "significant bits at position 0,4,8,12" 那句注释）。
 *
 *    第 0 轮的 0xfff0 掩码清掉第 0..3 位，也就是 lane 0、1 = FB 下标 0。
 *    FB 下标 0 是占位素数，它的 FB_inv_info[0],[1] 和 [8],[9] 从来没被写过
 *    （全 0），乘法结果恒为 0，于是 pcmpeqw 恒真、那两位 movemask 恒为 1。
 *    不掩掉的话每轮都会凭空记一个下标 0 的素数。mpqs.cpp 的 C 参考就是从
 *    `for (j=1; ...)` 开始的，等价。
 *
 * 5) store 在 adcq 之前无条件执行：CF=0 时也写，只是覆盖上一格、再由后面
 *    CF=1 的那次 store 盖回去。nr 的实际增量 = CF 之和，所以「恒定写入、
 *    条件前进」这个模式不能改成 if。
 *
 * 6) `movw %r13w,10(relptr,nr,2)` 写的是 16 位，`movw nrw,8(relptr)` 也只写
 *    16 位。aux5w / nr 全部按 u16 算。
 *
 * 7) `cmpw $27,%nrw` 是 16 位比较（MPQS_TD_MAX_NDIV=27），posloop 里那条是
 *    64 位的 `cmpq $27,%nr`。两处照抄各自的位宽。
 *
 * 8) 约减之后的 qx 只剩低 32 位：`movl %eax,%r8d` 写 %r8d 会把高 32 位清零，
 *    返回值也是 `movl %r8d,%eax`。后续 divloop 全是 32 位蒙哥马利。
 *
 * 9) tdloopk / tdloopa 的内层是 `jmp divloop*` 而不是跳去下一个素数：同一个
 *    素数反复约减直到除不动。如果约减过程中 qx 恰好变成 1，这里会因为
 *    1*inv*p 的高 32 位不为零而死循环 —— 这是原汇编的行为，如实照抄。
 *    差分测试的输入按契约落在「qx 不会在 FBk / Adiv 段正好减到 1」的范围里。
 *
 * 10) plus/minus 的判据是 relocation 之前 `cmpq %rcx,%q` 设的 CF，
 *     qx 按 u64 无符号比较；`testq %aux5,%aux5` 里 aux5 = (pr>>32) + CF。
 *     这条等价于 C 里的 `if (pr<2^32 && qx>(pr<<32)) goto next`。
 *
 * 11) 序言里 `movq nr,nr1` 而 nr1 与 minus 同寄存器（%rsi），所以 nr1 捕获的是
 *     「minus 追加 0 之前」的 nr。
 *
 * 12) prod 段和 tdloop 段都用 `&mpqs_FB[0] - 4*nFBk_1` 这个字节指针再按 4 字节
 *     缩放去索引，等价于读 mpqs_FB[2*(ii-nFBk_1)]。minus 分支写进去的 ii=0 会
 *     让下标变成负数 —— 原汇编和 C 参考都这么读，差分测试在 mpqs_FB 前面垫了
 *     足够大的对齐填充，两侧读到的是同一块内存。
 *
 * 13) tdloop 里内层的 `jnz tdloop` 是「这个素数除不动，换下一个」，不是重试；
 *     而 divloopk / divloopa 的 `jnz divloop*` 是「重试同一个素数」。三个内层
 *     循环的出口不一样。
 *
 * 载入用 loadu 而不是 load：汇编那边是 movaps（要求 16 字节对齐），这里放宽成
 * 不对齐，取到的值完全一样，但少一个「数组碰巧没对齐就崩」的隐患。
 */

#include <stdint.h>

#include <immintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;

/* mpqs.cpp 里定义的那几个数组（都是 u16_t / u32_t / unsigned char）。
 * 每个 I 值有一份独立的命名空间（lasieve_I<N>），这里跟着走就行。 */
extern u16 mpqs_FB_inv_info[];
extern u16 mpqs_FB_start[];
extern u16 mpqs_td_begin;
extern u16 mpqs_nFBk_1;
extern u16 mpqs_FB[];
extern u32 mpqs_FB_inv[];
extern u16 mpqs_nFBk;
extern u16 mpqs_FBk[3];
extern u32 mpqs_FBk_inv[3];
extern u16 mpqs_nFB;
extern u16 mpqs_nAdiv_total;
extern u16 mpqs_Adiv_all[];
extern u32 mpqs_FB_A_inv[];
extern unsigned char mpqs_256_inv_table[128];

#define MPQS_TD_MAX_NDIV 27	/* MPQS_REL_ENTRIES-5，和 asm 里的 $27 一致 */

u32_t asm_td(u16 *relptr, u16 minus, u64 qx)
{
	const u16 *ii;		/* aux3: FB_inv_info 游标，每次 +16 个 u16 */
	const u16 *ss;		/* aux2: FB_start   游标，每次 +8  个 u16 */
	const unsigned char *fbb;	/* aux1: &mpqs_FB[0] - 4*nFBk_1（字节） */
	const u32 *inb;		/* aux2 在 tdloop 段: &mpqs_FB_inv[0] - nFBk_1 */
	const u16 *fk;
	const u32 *fki;
	const u16 *ada;
	const u32 *adai;
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5;
	u64 rcx, nr, nr1, pr, cf;
	u16 aux5w, aux5a;
	u32 aux4d, aux2d, inv, p, rr, t;
	u32 qxd;

	/* ---- 序言：ind 在 8 个 u16 lane 上复制；xmm5 清零 ---- */
	xmm5 = _mm_setzero_si128();
	xmm3 = _mm_set1_epi16((short)(u16)relptr[0]);
	nr = relptr[4];

	ii = mpqs_FB_inv_info;
	ss = mpqs_FB_start;
	rcx = mpqs_td_begin;
	aux5w = (u16)(mpqs_nFBk_1 + 4);

	/* ---- 第 0 轮：FB 下标 0..3 的前一半（只保留下标 1,2,3） ---- */
	xmm0 = _mm_loadu_si128((const __m128i *)ii);
	xmm1 = _mm_loadu_si128((const __m128i *)(ii + 8));
	xmm2 = _mm_loadu_si128((const __m128i *)ss);
	xmm4 = _mm_add_epi16(_mm_sub_epi16(xmm0, xmm2), xmm3);
	xmm4 = _mm_mullo_epi16(xmm4, xmm1);
	xmm2 = _mm_loadu_si128((const __m128i *)(ss + 8));
	ii += 16;
	ss += 8;
	xmm4 = _mm_mulhi_epu16(xmm0, xmm4);
	xmm0 = _mm_loadu_si128((const __m128i *)ii);
	xmm1 = _mm_loadu_si128((const __m128i *)(ii + 8));
	xmm2 = _mm_sub_epi16(xmm2, xmm3);
	xmm4 = _mm_cmpeq_epi16(xmm4, xmm5);
	/* 只考虑 FB 下标 nr 1,2,3：掩码清掉 lane 0、1（占第 0..3 位）。
	 * 见文件头第 4 条，下面 found3 里第一次 store 恒不前进就是由它造成的。 */
	aux4d = (u32)_mm_movemask_epi8(xmm4) & 0xfff0u;
	xmm4 = _mm_sub_epi16(xmm0, xmm2);
	if (aux4d != 0)
		goto sieve_found3;

sieve_loop2:
	aux5w = (u16)(aux5w + 4);
	rcx -= 4;
	ii += 16;
	ss += 8;
	if (rcx == 0)
		goto sieve_prod;

	xmm4 = _mm_mullo_epi16(xmm4, xmm1);
	xmm2 = _mm_sub_epi16(_mm_loadu_si128((const __m128i *)ss), xmm3);
	xmm4 = _mm_mulhi_epu16(xmm0, xmm4);
	xmm0 = _mm_loadu_si128((const __m128i *)ii);
	xmm1 = _mm_loadu_si128((const __m128i *)(ii + 8));
	xmm4 = _mm_cmpeq_epi16(xmm4, xmm5);
	aux4d = (u32)_mm_movemask_epi8(xmm4);
	xmm4 = _mm_sub_epi16(xmm0, xmm2);
	if (aux4d == 0)
		goto sieve_loop2;

	/* 命中：4 个素数。shrl $1 的 CF 取 e[1]（= lane0|lane1，FB base+0），
	 * 之后三次 shrl $4 的 CF 依次取 (e>>1)、(e>>5)、(e>>9) 的第 4 位，
	 * 即 e[5]、e[9]、e[13]，对应 FB base+1..3。移位后这些位落在 0/4/8/12。 */
	aux5w = (u16)(aux5w - 4);
	aux4d |= aux4d >> 2;
	cf = (aux4d >> 1) & 1;
	aux4d >>= 1;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	cf = (aux4d >> 4) & 1;
	aux4d >>= 4;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	cf = (aux4d >> 4) & 1;
	aux4d >>= 4;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	cf = (aux4d >> 4) & 1;
	aux4d >>= 4;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	goto sieve_loop2;

sieve_found3:
	/* 第 0 轮的命中分支：只发 3 次 store（FB 下标 1、2、3）。
	 * shrl $5 的 CF 取 e[5]，之后两次 shrl $4 取 e[9]、e[13]。 */
	aux5w = (u16)(aux5w - 3);
	aux4d |= aux4d >> 2;
	cf = (aux4d >> 5) & 1;
	aux4d >>= 5;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	cf = (aux4d >> 4) & 1;
	aux4d >>= 4;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	cf = (aux4d >> 4) & 1;
	aux4d >>= 4;
	relptr[5 + nr] = aux5w;
	nr += cf;
	aux5w = (u16)(aux5w + 1);
	goto sieve_loop2;

sieve_prod:
	/* ---- 乘积 pr = ∏ FB[2*(rels[5+i]-nFBk_1)]，模 2^64 ---- */
	fbb = (const unsigned char *)mpqs_FB - 4 * (size_t)mpqs_nFBk_1;
	pr = 1;
	for (rcx = 0; rcx < nr; rcx++) {
		cf = relptr[5 + rcx];
		/* movzwq：只取 16 位。不能读 u32 —— mpqs_FB[2*j] 是素数低 16 位、
		 * [2*j+1] 是高 16 位，32 位读在素数 > 65535 时和汇编取到的值不同，
		 * 乘积随之错，整除判断跟着错。 */
		p = *(const u16 *)(fbb + 4 * cf);
		pr = (u64)(pr * (u64)p);
	}

	/* nr1 和 minus 同寄存器：nr1 捕获的是追加 0 之前的 nr */
	nr1 = nr;
	if (minus & 1) {
		relptr[5 + nr] = 0;
		nr++;
	}

posloop:
	/* while (!(qx&1)) { rels[5+nr]=nFBk_1; nr++; qx>>=1; }，上限 27 */
	if (qx & 1)
		goto odd;
	nr++;
	if (nr >= MPQS_TD_MAX_NDIV)
		return 0;
	relptr[4 + nr] = mpqs_nFBk_1;
	qx >>= 1;
	goto posloop;

odd:
	/* ---- qx * pr^{-1} mod 2^32，以及 qx <= pr<<32 的可行性检查 ----
	 * CF 由 cmpq %rcx,%q 设；aux5 = (pr>>32) + CF，为 0 就直接放弃。 */
	aux2d = mpqs_256_inv_table[(pr & 0xff) >> 1];
	rcx = (pr << 32) + 1;
	cf = (qx < rcx) ? 1 : 0;
	rcx = (pr >> 32) + cf;
	if (rcx == 0)
		return 0;

	inv = aux2d;
	t = (u32)pr * inv;	/* mull：低 32 位 */
	t &= 0x0000ff00u;
	t *= inv;
	inv -= t;
	t = (u32)pr * inv;
	t &= 0xffff0000u;
	t *= inv;
	inv -= t;
	qxd = (u32)qx * inv;
	qx = qxd;			/* movl %eax,%r8d：高位清零 */

	/* ---- 对筛出来的素数做蒙哥马利除法（tdloop） ---- */
	inb = (const u32 *)mpqs_FB_inv - (size_t)mpqs_nFBk_1;
	while (nr1 != 0) {
		qxd = (u32)qx;
		aux5w = relptr[4 + nr1];
		nr1--;
		p = *(const u32 *)(fbb + 4 * (u64)aux5w);
		inv = inb[aux5w];
		for (;;) {
			rr = qxd * inv;
			rcx = (u64)rr * p;
			if ((u32)(rcx >> 32) != 0)
				break;	/* jnz tdloop：换下一个素数 */
			if ((u16)nr >= MPQS_TD_MAX_NDIV)
				return 0;
			relptr[5 + nr] = aux5w;
			nr++;
			qxd = rr;
		}
		qx = qxd;
	}
	if (qxd == 1)
		goto td_end;

	/* ---- mpqs_FBk 的三个素数（tdloopk） ---- */
	fk = mpqs_FBk;
	fki = mpqs_FBk_inv;
	nr1 = (u64)mpqs_nFBk + 1;
	for (;;) {
		nr1--;
		qxd = (u32)qx;
		if (nr1 == 0)
			break;
		p = fk[nr1 - 1];
		inv = fki[nr1 - 1];
		for (;;) {
			rr = qxd * inv;
			rcx = (u64)rr * p;
			if ((u32)(rcx >> 32) != 0)
				break;
			if ((u16)nr >= MPQS_TD_MAX_NDIV)
				return 0;
			relptr[5 + nr] = (u16)nr1;
			nr++;
			qxd = rr;
		}
		qx = qxd;
	}
	if (qxd == 1)
		goto td_end;

	/* ---- mpqs_Adiv_all（tdloopa） ---- */
	ada = mpqs_Adiv_all;
	adai = mpqs_FB_A_inv;
	aux5a = (u16)(mpqs_nFB + mpqs_nFBk);
	nr1 = (u64)mpqs_nAdiv_total + 1;
	for (;;) {
		nr1--;
		qxd = (u32)qx;
		if (nr1 == 0)
			break;
		p = ada[nr1 - 1];
		inv = adai[nr1 - 1];
		for (;;) {
			rr = qxd * inv;
			rcx = (u64)rr * p;
			if ((u32)(rcx >> 32) != 0)
				break;
			if ((u16)nr >= MPQS_TD_MAX_NDIV)
				return 0;
			aux5a = (u16)(aux5a + (u16)nr1);
			relptr[5 + nr] = aux5a;
			nr++;
			aux5a = (u16)(aux5a - (u16)nr1);
			qxd = rr;
		}
		qx = qxd;
	}

td_end:
	relptr[4] = (u16)nr;
	return (u32)qx;
}
}  /* namespace lasieve_ns */
